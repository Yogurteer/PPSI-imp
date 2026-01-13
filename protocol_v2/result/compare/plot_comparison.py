#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
性能对比图绘制脚本
对比APSI和Our Scheme在sender size=1048576时的性能
自动从CSV文件读取最新数据
"""

import matplotlib.pyplot as plt
import numpy as np
import os
import csv

# 获取脚本所在目录
script_dir = os.path.dirname(os.path.abspath(__file__))
result_dir = os.path.dirname(script_dir)

# 设置中文字体支持
plt.rcParams['font.sans-serif'] = ['DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

def parse_apsi_label(label):
    """解析APSI的label，格式: sender_receiver_intersect_labelbytes_itembytes_thread"""
    parts = label.split('_')
    return {
        'sender': int(parts[0]),
        'receiver': int(parts[1]),
        'intersect': int(parts[2]),
        'label_bytes': int(parts[3]),
        'item_bytes': int(parts[4]),
        'thread': int(parts[5].replace('t', ''))
    }

def read_apsi_data(csv_path, sender_size=1048576, thread_num=1):
    """读取APSI性能数据"""
    receiver_sizes = []
    com_kb = []
    offline_time = []
    online_time = []
    
    # 用于处理重复receiver size的情况：优先选择特定参数文件
    preferred_params = {256: '1M-256.json'}  # receiver=256时优先选择1M-256.json
    data_dict = {}  # {receiver_size: [com, offline, online, param_file]}
    
    with open(csv_path, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            label = row['label(sender size+receiver size+intersecting size+label bytes+item bytes+thread num)']
            params = parse_apsi_label(label)
            
            # 只选择sender=1048576且thread=1的数据
            if params['sender'] == sender_size and params['thread'] == thread_num:
                receiver = params['receiver']
                com = float(row['com'].replace(' KB', ''))
                off_time = float(row['offline time'].replace(' s', ''))
                on_time = float(row['online time'].replace(' s', ''))
                param_file = row['param']
                
                # 如果该receiver size已存在，根据优先级选择
                if receiver in data_dict:
                    # 如果有优先参数文件，则选择该参数文件的数据
                    if receiver in preferred_params:
                        if param_file == preferred_params[receiver]:
                            data_dict[receiver] = [com, off_time, on_time, param_file]
                    # 否则保留第一个遇到的数据（已在字典中）
                else:
                    data_dict[receiver] = [com, off_time, on_time, param_file]
    
    # 转换为列表并排序
    for receiver in sorted(data_dict.keys()):
        receiver_sizes.append(receiver)
        com_kb.append(data_dict[receiver][0])
        offline_time.append(data_dict[receiver][1])
        online_time.append(data_dict[receiver][2])
    
    return receiver_sizes, com_kb, offline_time, online_time

def read_our_scheme_data(csv_path, sender_size=1048576):
    """读取Our Scheme性能数据"""
    receiver_sizes = []
    com_mb = []
    offline_time = []
    online_time = []
    
    with open(csv_path, 'r') as f:
        lines = f.readlines()
        # 跳过第一行注释，从第二行开始读取列名
        header = lines[1].strip().split(',')
        
        for line in lines[2:]:  # 从第三行开始读取数据
            values = line.strip().split(',')
            if len(values) >= len(header):
                row_dict = dict(zip(header, values))
                sender = int(row_dict['Sender'])
                receiver = int(row_dict['Receiver'])
                
                # 只选择sender=1048576的数据
                if sender == sender_size:
                    receiver_sizes.append(receiver)
                    com_mb.append(float(row_dict['com']))
                    offline_time.append(float(row_dict['sum_offline']))
                    online_time.append(float(row_dict['sum_online']))
    
    # 按receiver_size排序
    sorted_data = sorted(zip(receiver_sizes, com_mb, offline_time, online_time))
    receiver_sizes, com_mb, offline_time, online_time = zip(*sorted_data) if sorted_data else ([], [], [], [])
    
    return list(receiver_sizes), list(com_mb), list(offline_time), list(online_time)

# 读取APSI数据
apsi_csv_path = os.path.join(result_dir, 'APSI_Performance.csv')
apsi_receiver_sizes, apsi_com_kb, apsi_offline_time, apsi_online_time = read_apsi_data(apsi_csv_path)

# 读取Our Scheme数据
our_csv_path = os.path.join(result_dir, 'Ourscheme_Performance.csv')
our_receiver_sizes, our_com_mb, our_offline_time, our_online_time = read_our_scheme_data(our_csv_path)

# 转换APSI通信量为MB
apsi_com_mb = [x / 1024 for x in apsi_com_kb]

print(f"APSI数据点: {len(apsi_receiver_sizes)} 个")
print(f"Our Scheme数据点: {len(our_receiver_sizes)} 个")

# 创建图1: 通信开销对比
plt.figure(figsize=(10, 6))
plt.plot(apsi_receiver_sizes, apsi_com_mb, marker='o', linewidth=2, markersize=8, color='#1f77b4', label='APSI (thread=1)')
plt.plot(our_receiver_sizes, our_com_mb, marker='s', linewidth=2, markersize=8, color='#ff7f0e', label='Our Scheme')
plt.xlabel('Receiver Size', fontsize=12)
plt.ylabel('Communication (MB)', fontsize=12)
plt.title('Communication Cost Comparison (Sender Size = 1,048,576)', fontsize=14, fontweight='bold')
plt.grid(True, alpha=0.3, linestyle='--')
plt.legend(fontsize=11)
plt.xscale('log', base=2)
plt.yscale('log')
plt.xticks(our_receiver_sizes, [str(x) for x in our_receiver_sizes])
plt.tight_layout()
plt.savefig(os.path.join(script_dir, 'communication_comparison.png'), dpi=300, bbox_inches='tight')
print("通信开销对比图已保存: communication_comparison.png")
plt.close()

# 创建图2: 在线计算时间对比
plt.figure(figsize=(10, 6))
plt.plot(apsi_receiver_sizes, apsi_online_time, marker='o', linewidth=2, markersize=8, color='#1f77b4', label='APSI (thread=1)')
plt.plot(our_receiver_sizes, our_online_time, marker='s', linewidth=2, markersize=8, color='#ff7f0e', label='Our Scheme')
plt.xlabel('Receiver Size', fontsize=12)
plt.ylabel('Online Time (s)', fontsize=12)
plt.title('Online Computation Time Comparison (Sender Size = 1,048,576)', fontsize=14, fontweight='bold')
plt.grid(True, alpha=0.3, linestyle='--')
plt.legend(fontsize=11)
plt.xscale('log', base=2)
plt.yscale('log')
plt.xticks(our_receiver_sizes, [str(x) for x in our_receiver_sizes])
plt.tight_layout()
plt.savefig(os.path.join(script_dir, 'online_time_comparison.png'), dpi=300, bbox_inches='tight')
print("在线计算时间对比图已保存: online_time_comparison.png")
plt.close()

# 创建图3: 离线计算时间对比
plt.figure(figsize=(10, 6))
plt.plot(apsi_receiver_sizes, apsi_offline_time, marker='o', linewidth=2, markersize=8, color='#1f77b4', label='APSI (thread=1)')
plt.plot(our_receiver_sizes, our_offline_time, marker='s', linewidth=2, markersize=8, color='#ff7f0e', label='Our Scheme')
plt.xlabel('Receiver Size', fontsize=12)
plt.ylabel('Offline Time (s)', fontsize=12)
plt.title('Offline Computation Time Comparison (Sender Size = 1,048,576)', fontsize=14, fontweight='bold')
plt.grid(True, alpha=0.3, linestyle='--')
plt.legend(fontsize=11)
plt.xscale('log', base=2)
plt.yscale('log')
plt.yticks([10**2, 10**3, 10**4], ['$10^2$', '$10^3$', '$10^4$'])
plt.xticks(our_receiver_sizes, [str(x) for x in our_receiver_sizes])
plt.tight_layout()
plt.savefig(os.path.join(script_dir, 'offline_time_comparison.png'), dpi=300, bbox_inches='tight')
print("离线计算时间对比图已保存: offline_time_comparison.png")
plt.close()

print("\n所有对比图已成功生成!")
print("- communication_comparison.png: 通信开销对比")
print("- online_time_comparison.png: 在线计算时间对比")
print("- offline_time_comparison.png: 离线计算时间对比")
