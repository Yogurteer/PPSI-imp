import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# 设置绘图风格，使其美观且适合论文发表
# 使用 seaborn 的样式作为基础 (如果未安装 seaborn，可以注释掉这两行)
try:
    import seaborn as sns
    sns.set_theme(style="whitegrid", context="paper", font_scale=1.2)
    sns.set_style("ticks")
except ImportError:
    plt.style.use('ggplot')

# 读取数据
script_dir = os.path.dirname(os.path.abspath(__file__))
file_name = f'{script_dir}/online_statistic_summary.csv'
df = pd.read_csv(file_name)

# 定义需要堆叠的列 (各阶段开销)
# 对应: OPRF_s, Gen_Idx_s, Query_Idx_s, Get_Key_s, Dec_s
stack_cols = ['OPRF_s', 'Gen_Idx_s', 'Query_Idx_s', 'Get_Key_s', 'Dec_s']

# 定义图例标签 (去掉 _s 后缀，使其更符合英语习惯)
legend_labels = ['OPRF', 'Gen Index', 'Query Index', 'Get Key', 'Decryption']

# 定义配色 (选用对比度高且柔和的颜色，适合黑白打印或屏幕阅读)
# 顺序对应: OPRF, Gen_Idx, Query_Idx, Get_Key, Dec
colors = ['#4e79a7', '#f28e2b', '#e15759', '#76b7b2', '#59a14f']

# 将时间单位从秒(s)转换为毫秒(ms)
df_ms = df.copy()
for col in stack_cols:
    df_ms[col] = df_ms[col] * 1000

# 获取所有的 Sender 类型
senders = sorted(df_ms['Sender'].unique())

# 创建画布，2行2列 (因为有4个sender)
fig, axes = plt.subplots(2, 2, figsize=(16, 10), sharey=False)
axes = axes.flatten()  # 将2x2数组展平为1维数组，方便索引

# 遍历每个 Sender Size 进行绘图
for i, sender in enumerate(senders):
    ax = axes[i]
    
    # 筛选当前 Sender 的数据，并按 Receiver 排序
    data = df_ms[df_ms['Sender'] == sender].sort_values('Receiver')
    
    receivers = data['Receiver'].astype(str).values
    x_pos = np.arange(len(receivers))
    
    # 初始化底部高度为0
    bottom = np.zeros(len(receivers))
    
    # 循环堆叠每一个阶段
    for j, col in enumerate(stack_cols):
        values = data[col].values
        # 绘制柱状图
        ax.bar(x_pos, values, bottom=bottom, label=legend_labels[j], 
               color=colors[j], width=0.6, edgecolor='black', linewidth=0.5, alpha=0.9)
        # 更新底部高度
        bottom += values
        
    # 设置标题和轴标签
    ax.set_title(f'Sender Size: {sender}', fontsize=14, fontweight='bold', pad=10)
    ax.set_xlabel('Receiver Size', fontsize=12)
    
    # 设置 X 轴刻度
    ax.set_xticks(x_pos)
    ax.set_xticklabels(receivers, fontsize=10, rotation=45, ha='right')
    
    # 在左侧两个图显示 Y 轴标签
    if i % 2 == 0:
        ax.set_ylabel('Latency Overhead (ms)', fontsize=12)

    # 设置网格线 (仅Y轴)
    ax.grid(axis='y', linestyle='--', alpha=0.5)
    
    # 移除顶部和右侧的边框
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    
    # 为每个子图添加图例
    ax.legend(loc='upper left', frameon=True, fontsize=9, framealpha=0.9, fancybox=True)

# 调整布局以防止重叠
plt.tight_layout()

# 保存图片
output_path = os.path.join(script_dir, 'micro_online_runtime.png')
plt.savefig(output_path, dpi=300, bbox_inches='tight')
plt.show()
print(f"Figure saved to {output_path}")