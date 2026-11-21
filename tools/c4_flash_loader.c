/* RAM-only W25Q64 programmer. No MCU flash writes, no interrupts or HAL.
 * STM32F103xB header definitions: RCC/AFIO/GPIO/SPI from RM0008 ch.7,9,25.
 * Board main.h: PA5/6/7 SPI1, PB0 W25 CS, PB5 TFT CS (must stay high).
 * Mailbox 0x20000f00: command,status,address,length,expected_fnv,actual_fnv.
 * Commands: 1 erase aligned sectors; 2 program+verify; 3 read for backup.
 */
#include "stm32f1xx.h"

typedef struct {
    volatile uint32_t command, status, address, length, expected, actual;
} Mailbox;
#define M ((Mailbox *)0x20000F00UL)
#define BUFFER ((uint8_t *)0x20001000UL)
#define CAPACITY 12288U
#ifndef W25_ASSET_BASE
#define W25_ASSET_BASE 0x100000U
#define W25_ASSET_END  0x200000U
#endif

static uint8_t transfer(uint8_t byte)
{
    uint32_t wait = 1000000U;
    while (!(SPI1->SR & SPI_SR_TXE) && --wait) {}
    if (!wait) { M->status = 0xE1U; return 0xFFU; }
    *(__IO uint8_t *)&SPI1->DR = byte;
    wait = 1000000U;
    while (!(SPI1->SR & SPI_SR_RXNE) && --wait) {}
    if (!wait) { M->status = 0xE1U; return 0xFFU; }
    return *(__IO uint8_t *)&SPI1->DR;
}
static void select(void) { GPIOB->BRR = GPIO_BRR_BR0; }
static void deselect(void) { GPIOB->BSRR = GPIO_BSRR_BS0; }
static void address(uint8_t command, uint32_t at)
{
    select(); transfer(command); transfer((uint8_t)(at >> 16));
    transfer((uint8_t)(at >> 8)); transfer((uint8_t)at);
}
static int ready(void)
{
    for (uint32_t i = 0U; i < 1000000U; ++i) {
        select(); transfer(5U); uint8_t status = transfer(0xFFU); deselect();
        if (M->status == 0xE1U) { return 0; }
        if (!(status & 1U)) { return 1; }
    }
    return 0;
}
static int enable_write(void)
{
    select(); transfer(6U); deselect();
    select(); transfer(5U); uint8_t status = transfer(0xFFU); deselect();
    return (status & 2U) != 0U && M->status != 0xE1U;
}

__attribute__((noreturn)) void loader_main(void)
{
    SysTick->CTRL = 0U;
    RCC->CR |= RCC_CR_HSION;
    while (!(RCC->CR & RCC_CR_HSIRDY)) {}
    RCC->CFGR = 0U;
    while (RCC->CFGR & RCC_CFGR_SWS) {}
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN | RCC_APB2ENR_SPI1EN;
    /* Default SPI1 mapping and SWJ_CFG=000 keep SWD available. */
    AFIO->MAPR = 0U;
    GPIOB->BSRR = GPIO_BSRR_BS0 | GPIO_BSRR_BS5;
    GPIOB->CRL = (GPIOB->CRL & ~0x00F0000FUL) | 0x00300003UL;
    GPIOA->CRL = (GPIOA->CRL & ~0xFFF00000UL) | 0xB4B00000UL;
    SPI1->CR1 = 0U;
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_SPE;
    M->command = 0U;
    M->status = 0x10U;
    for (;;) {
        uint32_t command = M->command;
        if (command == 0U) { continue; }
        M->status = 1U;
        uint32_t at = M->address, length = M->length;
        int ok = at >= W25_ASSET_BASE && at < W25_ASSET_END && length != 0U &&
                 length <= W25_ASSET_END - at;
        select(); transfer(0x9FU); uint8_t maker = transfer(0xFFU);
        transfer(0xFFU); uint8_t capacity = transfer(0xFFU); deselect();
        ok = ok && maker != 0U && maker != 0xFFU && capacity == 0x17U;
        if (ok && command == 1U) {
            ok = (at % 4096U == 0U && length % 4096U == 0U);
            for (uint32_t i = 0U; ok && i < length; i += 4096U) {
                ok = enable_write();
                if (ok) { address(0x20U, at + i); deselect(); ok = ready(); }
            }
        } else if (ok && command == 2U && length <= CAPACITY) {
            uint32_t offset = 0U;
            while (ok && offset < length) {
                uint32_t count = 256U - (at + offset) % 256U;
                if (count > length - offset) { count = length - offset; }
                ok = enable_write();
                if (ok) {
                    address(2U, at + offset);
                    for (uint32_t i = 0U; i < count; ++i) { transfer(BUFFER[offset + i]); }
                    deselect(); ok = ready();
                }
                offset += count;
            }
            uint32_t hash = 2166136261UL;
            address(3U, at);
            for (uint32_t i = 0U; i < length; ++i) { hash = (hash ^ transfer(0xFFU)) * 16777619UL; }
            deselect(); M->actual = hash;
            ok = ok && hash == M->expected;
        } else if (ok && command == 3U && length <= CAPACITY) {
            address(3U, at);
            for (uint32_t i = 0U; i < length; ++i) { BUFFER[i] = transfer(0xFFU); }
            deselect();
        } else { ok = 0; }
        if (M->status == 0xE1U) { ok = 0; }
        M->command = 0U;
        M->status = ok ? 2U : 0xFFU;
    }
}

__attribute__((naked, section(".entry"), noreturn)) void loader_entry(void)
{
    __asm volatile ("cpsid i\nldr sp, =0x20005000\nb loader_main");
}
