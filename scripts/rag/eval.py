#!/usr/bin/env python3
"""金标集评测：驱动 tamias_rag_cli 算 recall@K，低于门槛就非零退出。

见 docs/PLAN-RAG.md §3 M4、§7。

**不在这里重实现分词**。评测必须跑发布的那份代码 —— Python 与 C++ 双实现的分词
必然漂移，那样测过的不一定是发出去的（PLAN-RAG §9）。

用法:
    python scripts/rag/eval.py --cli build/bin/Debug/tamias_rag_cli.exe
    python scripts/rag/eval.py --cli ... --threshold 0.9 --show-misses
"""

from __future__ import annotations

import argparse
import json
import statistics
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_GOLDEN = Path("tests/rag/golden_qa.jsonl")


def load_golden(path: Path) -> list[dict]:
    cases = []
    with path.open(encoding="utf-8") as handle:
        for number, line in enumerate(handle, start=1):
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            try:
                case = json.loads(line)
            except json.JSONDecodeError as error:
                sys.exit(f"{path} 第 {number} 行不是合法 JSON：{error}")
            for field in ("id", "question", "expect_paths"):
                if field not in case:
                    sys.exit(f"{path} 第 {number} 行缺少 {field}")
            if not case["expect_paths"]:
                sys.exit(f"{path} 第 {number} 行的 expect_paths 是空的")
            cases.append(case)
    if not cases:
        sys.exit(f"{path} 里一道题都没有")
    return cases


def ask(cli: Path, question: str, corpus: str, k: int) -> tuple[list[dict], float]:
    command = [str(cli), f"--k={k}", f"--query={question}"]
    if corpus:
        command.append(f"--corpus={corpus}")
    started = time.perf_counter()
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    if completed.returncode != 0:
        sys.exit(f"tamias_rag_cli 退出码 {completed.returncode}：\n{completed.stderr.strip()}")
    try:
        return json.loads(completed.stdout), elapsed_ms
    except json.JSONDecodeError as error:
        sys.exit(f"tamias_rag_cli 的输出不是 JSON：{error}\n{completed.stdout[:400]}")


def main() -> int:
    parser = argparse.ArgumentParser(description="RAG 金标集评测（recall@K）")
    parser.add_argument("--cli", required=True, type=Path, help="tamias_rag_cli 可执行文件")
    parser.add_argument("--golden", default=DEFAULT_GOLDEN, type=Path)
    parser.add_argument("--k", default=5, type=int)
    parser.add_argument("--threshold", default=0.8, type=float, help="recall@K 门槛")
    parser.add_argument("--show-misses", action="store_true", help="列出每道没命中的题")
    args = parser.parse_args()

    if not args.cli.is_file():
        sys.exit(f"找不到 {args.cli}（先构建 tamias_rag_cli）")

    cases = load_golden(args.golden)
    hits = 0
    latencies: list[float] = []
    misses: list[tuple[dict, list[str]]] = []

    for case in cases:
        results, elapsed_ms = ask(args.cli, case["question"], case.get("corpus", ""), args.k)
        latencies.append(elapsed_ms)
        found = {item["path"] for item in results}
        if found & set(case["expect_paths"]):
            hits += 1
        else:
            misses.append((case, [item["path"] for item in results]))

    recall = hits / len(cases)
    print(f"金标集 {len(cases)} 题，recall@{args.k} = {recall:.3f}（门槛 {args.threshold}）")
    print(f"单次查询耗时（含进程启动）：中位 {statistics.median(latencies):.0f} ms")

    if misses and args.show_misses:
        print(f"\n没命中的 {len(misses)} 题：")
        for case, got in misses:
            print(f"  [{case['id']}] {case['question']}")
            print(f"      期望 {case['expect_paths']}")
            print(f"      实际 {got[:args.k]}")

    if recall < args.threshold:
        print(f"\n低于门槛。用 --show-misses 看是哪几道；"
              f"修检索而不是改这里的期望值。", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
