import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
import os

# 获取脚本所在目录的绝对路径
script_dir = os.path.dirname(os.path.abspath(__file__))

# 1. 数据读取与预处理
# 注意:您的CSV文件第一行是元数据,真正的表头在第二行,因此设置 header=1
csv_path = os.path.join(script_dir, 'scheme_compare_online.csv')
df = pd.read_csv(csv_path, header=1)

# 转置数据：行变为 Receiver Data Size，列变为方案
plot_data = df.set_index('scheme').T

# -------------------------------------------------------------------------
# [核心修正]：处理 LaTeX 上标格式
# 定义一个函数，将 "2^10" 这种字符串转换为 "$2^{10}$" (带花括号)
# -------------------------------------------------------------------------
def format_latex_label(label):
    # 检查是否包含 '^' 符号
    if '^' in label:
        base, exponent = label.split('^')
        # f-string 中使用 {{ }} 来表示字面量的花括号
        # 结果会变成: $base^{exponent}$
        return f"${base}^{{{exponent}}}$"
    return label

# 应用转换到索引上
plot_data.index = [format_latex_label(x) for x in plot_data.index]

# 2. 绘图设置 (学术风格)
plt.rcParams['font.family'] = 'serif'   # 衬线字体
plt.rcParams['mathtext.fontset'] = 'cm' # 数学公式使用 Computer Modern 字体

fig, ax = plt.subplots(figsize=(8, 5))

# 定义颜色映射: Our Scheme用橙色, APSI用蓝色
colors = {'Our Scheme': '#ff7f0e', 'APSI': '#1f77b4'}  # 橙色和蓝色

# 绘制柱状图
# zorder=3 让柱子显示在网格线之上
plot_data.plot(kind='bar', ax=ax, width=0.7, rot=0, edgecolor='black', alpha=0.85, zorder=3, color=colors)

# 3. 图表修饰
ax.set_xlabel("Receiver Data Size", fontsize=12, labelpad=10, fontweight='bold')
ax.set_ylabel("Online Time (s)", fontsize=12, labelpad=10, fontweight='bold')

# 设置网格线 (zorder=0 让网格线在柱子后面)
ax.grid(axis='y', linestyle='--', alpha=0.5, zorder=0)

# 设置图例
ax.legend(title="Scheme", fontsize=10, loc='upper left')

# 紧凑布局
plt.tight_layout()

# 4. 保存与显示
# 保存为高分辨率图片,适合插入论文 (dpi=300)
output_path = os.path.join(script_dir, 'onlinetime_comparison.png')
plt.savefig(output_path, dpi=300, bbox_inches='tight')
plt.show()

print(f"Figure saved to {output_path}")