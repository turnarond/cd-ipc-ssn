#!/usr/bin/env python3
"""检查 SSN 文档、版本和测试事实是否保持一致。"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Finding:
    category: str
    path: str
    message: str


STALE_FACTS = (
    "14 个自动化套件",
    "581 例",
    "625 例",
    "701 个断言",
    "701 例",
    "1159 例",
    "1159 个断言",
    "1304 例",
    "1304 个断言",
    "1396 例",
    "1396 个断言",
    "test_protocol（25",
)
STALE_PATTERNS = ((re.compile(r"全量测试[^\r\n]{0,40}7 套件"), "全量测试……7 套件"),)
EXPECTED_SUITE_COUNT = 24
EXPECTED_ASSERTION_COUNT = 1481
EXPECTED_EXAMPLE_COUNT = 19
EXPECTED_PROTOCOL_COUNT = 31
TOTAL_SUITE_PATTERN = re.compile(
    r"(?:全部|全量|构建\s*\+)[^\r\n]{0,30}?(\d+)\s*个?\s*自动化套件"
)
KEY_FACT_PATTERNS = (
    (re.compile(r"(?P<claim>合计[:：]?\s*自动化\s*(?P<value>\d+)\s*套件)"),
     EXPECTED_SUITE_COUNT),
    (
        re.compile(
            r"(?P<claim>(?:全部|全量|合计[:：]?\s*自动化|自动化测试)"
            r"[^。\r\n]{0,60}?(?P<value>\d+)\s*个?\s*(?:断言|例))"
        ),
        EXPECTED_ASSERTION_COUNT,
    ),
    (
        re.compile(
            r"(?P<claim>(?:全部|全量|合计|示例构建)"
            r"[^。\r\n]{0,60}?(?P<value>\d+)\s*个?\s*示例)"
        ),
        EXPECTED_EXAMPLE_COUNT,
    ),
    (
        re.compile(
            r"(?P<claim>test_protocol(?!_)[^\r\n]{0,30}?[（(]"
            r"(?P<value>\d+))"
        ),
        EXPECTED_PROTOCOL_COUNT,
    ),
)
PROHIBITED_REFERENCES = ("CLAUDE.md", ".claude", ".remember", "superpowers")


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _version_findings(root: Path) -> list[Finding]:
    version = _read(root / "VERSION").strip()
    header = _read(root / "src/version/ssn_version.h")
    cmake = _read(root / "CMakeLists.txt")
    changelog = _read(root / "CHANGELOG.md")
    header_match = re.search(r'SSN_VERSION_STRING\s+"([^"]+)"', header)
    header_parts = [re.search(rf"SSN_VERSION_{name}\s+(\d+)", header) for name in ("MAJOR", "MINOR", "PATCH")]
    cmake_parts = [re.search(rf"set\(VERSION_{name}\s+(\d+)\)", cmake) for name in ("MAJOR", "MINOR", "PATCH")]
    changelog_match = re.search(r"^## \[(\d+\.\d+\.\d+)\]", changelog, re.MULTILINE)
    sources = {
        "VERSION": version,
        "头文件组件宏": ".".join(match.group(1) for match in header_parts if match),
        "头文件字符串": header_match.group(1) if header_match else "",
        "CMake": ".".join(match.group(1) for match in cmake_parts if match),
        "CHANGELOG 最新版本": changelog_match.group(1) if changelog_match else "",
    }
    if all(value == version for value in sources.values()):
        return []
    details = "，".join(f"{name}={value or '缺失'}" for name, value in sources.items())
    return [Finding("版本", "VERSION", f"版本口径不一致：{details}")]


def _active_documents(root: Path) -> list[Path]:
    files = [root / "README.md"] if (root / "README.md").exists() else []
    files.extend((root / "docs").rglob("*.md"))
    return [path for path in files if "09-归档" not in path.parts]


def _strip_code(text: str) -> str:
    text = re.sub(r"```.*?```", "", text, flags=re.DOTALL)
    return re.sub(r"`[^`\r\n]*`", "", text)


def _is_acceptance_snapshot(root: Path, path: Path) -> bool:
    """发布验收报告是发布时点快照：豁免事实扫描。

    双条件必须同时满足：位于部署手册目录、文件名含“发布验收报告”。
    治理类检查与链接检查不豁免。
    """
    relative = path.relative_to(root)
    return (
        relative.parent.as_posix() == "docs/05-部署手册"
        and "发布验收报告" in path.name
    )


def _document_findings(root: Path) -> list[Finding]:
    findings: list[Finding] = []
    for path in _active_documents(root):
        text = _read(path)
        relative = path.relative_to(root).as_posix()
        if not _is_acceptance_snapshot(root, path):
            for stale in STALE_FACTS:
                if stale in text:
                    findings.append(Finding("事实", relative, f"包含过期口径：{stale}"))
            for pattern, label in STALE_PATTERNS:
                if pattern.search(text):
                    findings.append(Finding("事实", relative, f"包含过期口径：{label}"))
            for match in TOTAL_SUITE_PATTERN.finditer(text):
                if int(match.group(1)) != EXPECTED_SUITE_COUNT:
                    findings.append(Finding("事实", relative, f"包含过期口径：{match.group(0)}"))
            for pattern, expected in KEY_FACT_PATTERNS:
                for match in pattern.finditer(text):
                    if int(match.group("value")) != expected:
                        findings.append(Finding("事实", relative, f"包含过期口径：{match.group('claim')}"))
            if "driver-sdk" in text.lower():
                findings.append(Finding("事实", relative, "包含其他项目的 driver-sdk 历史"))
        for reference in PROHIBITED_REFERENCES:
            if reference.lower() in text.lower():
                findings.append(Finding("治理", relative, f"引用 AI/插件中间接口：{reference}"))

        link_text = _strip_code(text)
        for match in re.finditer(r"\[[^\]\r\n]*\]\(([^)\r\n]+)\)", link_text):
            target = match.group(1).strip(" <>").split("#", 1)[0]
            if not target or re.match(r"^(?:https?:|mailto:)", target):
                continue
            resolved = (path.parent / target).resolve()
            if not resolved.exists():
                findings.append(Finding("链接", relative, f"目标不存在：{target}"))
    return findings


def _test_findings(root: Path) -> list[Finding]:
    script = _read(root / "test/run_tests.sh")
    suite_count = len(re.findall(r"^\s+(?:test_|example_)[a-z0-9_]+\s*(?:#.*)?$", script, re.MULTILINE))
    if suite_count == EXPECTED_SUITE_COUNT:
        return []
    return [Finding("事实", "test/run_tests.sh", f"自动化套件应为 {EXPECTED_SUITE_COUNT}，实际解析为 {suite_count}")]


def inspect_repository(root: Path) -> list[Finding]:
    root = root.resolve()
    return _version_findings(root) + _test_findings(root) + _document_findings(root)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd(), help="仓库根目录")
    args = parser.parse_args()
    findings = inspect_repository(args.root)
    for finding in findings:
        print(f"[{finding.category}] {finding.path}: {finding.message}")
    print(f"文档一致性检查：{len(findings)} 个问题")
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
