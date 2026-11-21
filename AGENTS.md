# SmartTone 团队 Git 协作规范

本文件只规定团队 Git 协作方式，不涉及项目业务逻辑。

## 长期分支

- `main`：稳定版本，用于正式交付。禁止直接向 `main` 推送。
- `dev`：团队日常开发与代码集成分支。
- `feature/*`：每项独立功能使用一个功能分支，从最新 `dev` 创建。例如：
  - `feature/audio`
  - `feature/display`
  - `feature/record`
  - `feature/ui`

## 日常开发流程

1. 开发前先同步最新 `dev`：

   ```bash
   git switch dev
   git fetch origin
   git pull --ff-only origin dev
   ```

2. 从最新 `dev` 创建功能分支：

   ```bash
   git switch -c feature/<功能名>
   ```

3. 在功能分支完成开发，提交并推送：

   ```bash
   git add <相关文件>
   git commit -m "<清晰的提交说明>"
   git push -u origin feature/<功能名>
   ```

4. 使用 Pull Request 将 `feature/*` 合并到 `dev`。
5. 合并前必须检查代码差异、编译结果和测试结果。
6. 未经负责人确认，不得合并 Pull Request。

## 分支保护规则

- 禁止强制推送，包括 `git push --force` 和 `git push --force-with-lease`。
- 不得覆盖其他成员尚未提交的修改。
- 遇到合并冲突时先暂停，保留现场并向负责人汇报，不要擅自覆盖他人代码。
- `main` 只接受经过确认的 Pull Request 合并，不直接提交日常开发改动。
- 不自动合并任何 Pull Request，不擅自修改团队成员权限。
