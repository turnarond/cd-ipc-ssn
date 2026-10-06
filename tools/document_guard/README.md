# 文档一致性检查工具

该工具使用 Python 标准库检查版本同步、自动化测试套件数量、Markdown 相对链接、过期统计口径和正式文档中的 AI/插件中间接口引用。

```bash
python tools/document_guard/check_docs.py
python -m unittest discover -s tools/document_guard/tests -v
```

退出码为 `0` 表示通过；发现问题时输出分类、文件和原因并返回 `1`。新增规则必须先在 `tests/test_check_docs.py` 中完成红—绿—重构循环。

## 版本基线维护

发布版本调整测试体系时，必须同步更新 `check_docs.py` 中的版本事实规则及对应单元测试。目前基线为 23 个自动化套件、1396 个断言、19 个示例，`test_protocol` 为 31 个断言。修改前先让新基线测试失败，再更新规则并验证正式文档，避免守卫继续接受旧数字。`docs/05-部署手册/` 下文件名含“发布验收报告”的文档作为发布时点事实快照，豁免事实类扫描；违禁引用与坏链接检查对其仍生效，豁免边界由单元测试锁死。
