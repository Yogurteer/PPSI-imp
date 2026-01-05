import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# 读取数据
filename = "对比结果.xlsx - Sheet1.csv"
df = pd.read_csv(filename)

# 准备绘图数据
# 提取X轴标签（即列名中的2^0, 2^6等）
x_labels = df.columns[1:]
# 提取对应的Y轴数值
our_scheme_vals = df.loc[df['Unnamed: 0'] == 'Our Scheme'].values[0][1:]
apsi_vals = df.loc[df['Unnamed: 0'] == 'APSI'].values[0][1:]

# 设置绘图参数
x = np.arange(len(x_labels))  # 标签位置
width = 0.35  # 柱状图宽度

# 创建图表
fig, ax = plt.subplots(figsize=(10, 6))

# 绘制柱状图
# 使用学术界常用的深蓝和深橙配色，并添加黑色边框增加清晰度
rects1 = ax.bar(x - width/2, our_scheme_vals, width, label='Our Scheme', color='#4c72b0', edgecolor='black', linewidth=0.5)
rects2 = ax.bar(x + width/2, apsi_vals, width, label='APSI', color='#dd8452', edgecolor='black', linewidth=0.5)

# 设置标签和标题
ax.set_xlabel('Receiver Size')
ax.set_ylabel('Online Time (s)')
ax.set_xticks(x)
ax.set_xticklabels(x_labels)
ax.legend()

# 添加网格线以辅助读数（仅保留Y轴网格，且置于底层）
ax.yaxis.grid(True, linestyle='--', which='major', color='grey', alpha=.25)
ax.set_axisbelow(True)

# 调整布局防止标签重叠
fig.tight_layout()

# 保存图片
output_filename = 'performance_comparison_bar_chart.png'
plt.savefig(output_filename, dpi=300)