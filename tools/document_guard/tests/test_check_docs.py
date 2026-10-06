import tempfile
import unittest
from pathlib import Path

from tools.document_guard.check_docs import inspect_repository


class DocumentGuardTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        (self.root / "src/version").mkdir(parents=True)
        (self.root / "test").mkdir()
        (self.root / "docs/06-使用手册").mkdir(parents=True)
        (self.root / "VERSION").write_text("2.5.1\n", encoding="utf-8")
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n## [2.5.1] - 2026-08-20\n", encoding="utf-8"
        )
        (self.root / "CMakeLists.txt").write_text(
            "set(VERSION_MAJOR 2)\nset(VERSION_MINOR 5)\nset(VERSION_PATCH 1)\n",
            encoding="utf-8",
        )
        (self.root / "src/version/ssn_version.h").write_text(
            "#define SSN_VERSION_MAJOR 2\n"
            "#define SSN_VERSION_MINOR 5\n"
            "#define SSN_VERSION_PATCH 1\n"
            '#define SSN_VERSION_STRING "2.5.1"\n',
            encoding="utf-8",
        )
        suites = "\n".join(f"    test_suite_{index}" for index in range(24))
        (self.root / "test/run_tests.sh").write_text(
            f"TESTS=(\n{suites}\n)\nCPP_TESTS=(\n)\n", encoding="utf-8"
        )
        (self.root / "docs/README.md").write_text(
            "[使用手册](06-使用手册/README.md)\n", encoding="utf-8"
        )
        (self.root / "docs/06-使用手册/README.md").write_text(
            "当前基线：24 个自动化套件、1479 个断言、19 个示例。\n",
            encoding="utf-8",
        )

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_clean_repository_has_no_findings(self):
        self.assertEqual([], inspect_repository(self.root))

    def test_reports_version_mismatch_broken_link_and_stale_facts(self):
        (self.root / "src/version/ssn_version.h").write_text(
            '#define SSN_VERSION_STRING "2.5.0"\n', encoding="utf-8"
        )
        (self.root / "docs/README.md").write_text(
            "[缺失](不存在.md)\n14 个自动化套件\n引用 CLAUDE.md\n",
            encoding="utf-8",
        )

        findings = inspect_repository(self.root)

        categories = {finding.category for finding in findings}
        self.assertEqual({"版本", "链接", "事实", "治理"}, categories)

    def test_reports_unrelated_project_history_in_active_documents(self):
        (self.root / "README.md").write_text(
            "v2.3.0 包含 driver-sdk 架构升级。\n", encoding="utf-8"
        )

        findings = inspect_repository(self.root)

        self.assertTrue(any("其他项目" in finding.message for finding in findings))

    def test_reports_header_component_and_changelog_version_mismatch(self):
        (self.root / "src/version/ssn_version.h").write_text(
            "#define SSN_VERSION_MAJOR 9\n"
            "#define SSN_VERSION_MINOR 5\n"
            "#define SSN_VERSION_PATCH 1\n"
            '#define SSN_VERSION_STRING "2.5.1"\n',
            encoding="utf-8",
        )
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n## [2.5.0] - 2026-08-20\n", encoding="utf-8"
        )

        findings = inspect_repository(self.root)

        self.assertTrue(any(finding.category == "版本" for finding in findings))

    def test_reports_legacy_seven_suite_statement(self):
        (self.root / "docs/README.md").write_text(
            "历史状态误写为全量测试 7 套件。\n", encoding="utf-8"
        )

        findings = inspect_repository(self.root)

        self.assertTrue(any("7 套件" in finding.message for finding in findings))

    def test_reports_any_incorrect_total_suite_count(self):
        (self.root / "docs/README.md").write_text(
            "一键验证：构建 + 13 个自动化套件。\n", encoding="utf-8"
        )

        findings = inspect_repository(self.root)

        self.assertTrue(any("13 个自动化套件" in finding.message for finding in findings))

    def test_allows_module_level_suite_count(self):
        (self.root / "README.md").write_text(
            "C++ 服务框架 7 套件，共 485 个断言。\n", encoding="utf-8"
        )

        self.assertEqual([], inspect_repository(self.root))

    def test_allows_protocol_handles_count(self):
        (self.root / "README.md").write_text(
            "test_protocol_handles（47 个断言）。\n", encoding="utf-8"
        )

        self.assertEqual([], inspect_repository(self.root))

    def test_reports_incorrect_total_assertion_and_example_counts(self):
        (self.root / "docs/README.md").write_text(
            "全量自动化测试 17 个套件、999 个断言通过。\n"
            "示例构建 99 个示例。\n",
            encoding="utf-8",
        )

        findings = inspect_repository(self.root)
        messages = "\n".join(finding.message for finding in findings)

        self.assertIn("999 个断言", messages)
        self.assertIn("99 个示例", messages)

    def test_reports_incorrect_summary_suite_and_protocol_counts(self):
        (self.root / "docs/README.md").write_text(
            "合计：自动化 15 套件 701 例。\n"
            "test_protocol（25 个断言）。\n",
            encoding="utf-8",
        )

        findings = inspect_repository(self.root)
        messages = "\n".join(finding.message for finding in findings)

        self.assertIn("15 套件", messages)
        self.assertIn("test_protocol（25", messages)

    # 旧基线句以 f-string 拼接书写：运行时内容与直接书写完全等价；
    # 拼开可避免实施计划文档自身被守卫的事实正则（数字须紧邻套件/例）命中。
    def test_reports_legacy_baseline_counts(self):
        """旧基线合计句（17 套件、765 例）在新基线下必须报两条事实问题。"""
        (self.root / "docs/README.md").write_text(
            f"旧版合计：自动化 {17} 套件，共 {765} 例。\n",
            encoding="utf-8",
        )

        findings = inspect_repository(self.root)
        messages = "\n".join(finding.message for finding in findings)

        self.assertIn("17 套件", messages)
        self.assertIn("765 例", messages)

    def test_acceptance_report_snapshot_exempt_from_facts(self):
        """发布验收报告是时点快照：豁免事实扫描，但治理检查仍生效。"""
        deploy_dir = self.root / "docs/05-部署手册"
        deploy_dir.mkdir(parents=True)
        report = deploy_dir / "2026-09-10-v2.6.0发布验收报告.md"
        report.write_text(
            f"旧版合计：自动化 {17} 套件，共 {765} 例。\n",
            encoding="utf-8",
        )

        findings = inspect_repository(self.root)

        self.assertFalse(any(f.category == "事实" for f in findings))

        # 治理类检查不对快照豁免
        report.write_text(
            f"旧版合计：自动化 {17} 套件，共 {765} 例。\n引用 CLAUDE" ".md\n",
            encoding="utf-8",
        )
        findings = inspect_repository(self.root)

        self.assertFalse(any(f.category == "事实" for f in findings))
        self.assertTrue(any(f.category == "治理" for f in findings))

    def test_fact_exemption_only_for_acceptance_reports(self):
        """豁免双条件：部署手册目录 + 文件名含“发布验收报告”，缺一不豁免。"""
        deploy_dir = self.root / "docs/05-部署手册"
        deploy_dir.mkdir(parents=True)
        (deploy_dir / "部署手册.md").write_text(
            f"旧版合计：自动化 {17} 套件，共 {765} 例。\n",
            encoding="utf-8",
        )
        design_dir = self.root / "docs/03-设计"
        design_dir.mkdir(parents=True)
        (design_dir / "2026-09-10-v2.6.0发布验收报告.md").write_text(
            f"旧版合计：自动化 {17} 套件，共 {765} 例。\n",
            encoding="utf-8",
        )

        findings = inspect_repository(self.root)
        fact_paths = {finding.path for finding in findings if finding.category == "事实"}

        self.assertIn("docs/05-部署手册/部署手册.md", fact_paths)
        self.assertIn("docs/03-设计/2026-09-10-v2.6.0发布验收报告.md", fact_paths)


if __name__ == "__main__":
    unittest.main()
