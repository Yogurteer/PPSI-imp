#!/usr/bin/env python3
"""
生成数据脚本

默认生成 2**24 行，每行格式：<keyword> <8bit_binary>\n
示例： python3 gen_data.py --out data/keys_payloads.txt

性能说明：使用分块缓冲写入以减少 I/O 次数。关键字长度在 1 到 10 字符之间。
"""

import argparse
import secrets
import string
import sys
import time


def generate_lines(out_path: str, total: int, chunk_size: int, max_keyword_len: int, charset: str):
	alphabet = charset
	max_len = max_keyword_len

	start = time.time()
	written = 0
	
	# 使用集合确保关键字唯一
	used_keywords = set()
	max_attempts = 1000  # 每个关键字最大尝试次数
	
	# 使用文本模式写入
	with open(out_path, "w", encoding="utf-8", buffering=1 << 20) as f:
		buf = []
		append = buf.append
		for i in range(total):
			# 生成唯一关键字
			kw = None
			for attempt in range(max_attempts):
				# klen 关键字长度至少为 3
				klen = secrets.randbelow(max_len - 2) + 3
				# 生成关键字
				candidate = ''.join(secrets.choice(alphabet) for _ in range(klen))
				if candidate not in used_keywords:
					kw = candidate
					used_keywords.add(kw)
					break
			
			if kw is None:
				print(f"警告: 在生成第 {i+1} 个关键字时达到最大尝试次数，可能需要增加关键字长度", file=sys.stderr)
				# 回退策略：使用索引生成唯一关键字
				kw = f"key_{i}"
				used_keywords.add(kw)
			
			payload = format(secrets.randbits(8), '08b')
			append(f"{kw} {payload}\n")

			# flush when buffer reaches chunk_size
			if len(buf) >= chunk_size:
				f.write(''.join(buf))
				written += len(buf)
				buf.clear()

				# 简单进度输出（每写入若干块显示一次）
				if written % (max(1, (1 << 20) // chunk_size)) == 0:
					passed = time.time() - start
					perc = written / total * 100
					print(f"written {written}/{total} ({perc:.2f}%) - elapsed {passed:.1f}s", file=sys.stderr)

		# flush remainder
		if buf:
			f.write(''.join(buf))
			written += len(buf)

	elapsed = time.time() - start
	print(f"Done. Wrote {total} lines ({len(used_keywords)} unique keywords) to {out_path} in {elapsed:.1f}s", file=sys.stderr)


def parse_args():
	parser = argparse.ArgumentParser(description="Generate data with keyword and 8-bit binary payload per line.")
	parser.add_argument("--out", "-o", default="data/kv_2_24.txt",
						help="输出文件路径 (默认: data/kv_2_24.txt)")
	parser.add_argument("--lines", "-n", type=int, default=2 ** 24,
						help="要生成的行数 (默认: 2**24)")
	parser.add_argument("--chunk-size", "-c", type=int, default=65536,
						help="每次写入的行数缓冲区大小 (默认: 65536)")
	parser.add_argument("--max-keyword-len", "-m", type=int, default=50,
						help="关键字最大长度，最小 1 (默认: 50)")
	parser.add_argument("--charset", type=str, default=string.ascii_lowercase + string.digits,
						help="关键字字符集 (默认: a-z0-9)")
	return parser.parse_args()


def main():
	args = parse_args()

	if args.max_keyword_len < 1 or args.max_keyword_len > 1024:
		print("max-keyword-len 必须在 1 到 1024 之间", file=sys.stderr)
		sys.exit(1)
	if args.lines <= 0:
		print("lines 必须是正整数", file=sys.stderr)
		sys.exit(1)
	if args.chunk_size <= 0:
		print("chunk-size 必须是正整数", file=sys.stderr)
		sys.exit(1)

	# 确保输出目录存在
	try:
		generate_lines(args.out, args.lines, args.chunk_size, args.max_keyword_len, args.charset)
	except Exception as e:
		print("Error:", e, file=sys.stderr)
		sys.exit(1)


if __name__ == "__main__":
	main()

