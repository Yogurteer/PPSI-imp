# Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT license.

# example usage:
# python data/test_data_creator.py 1024 512 256 32 8

import argparse
import random
import string
import os
script_dir = os.path.dirname(os.path.abspath(__file__))

ap = argparse.ArgumentParser()
ap.add_argument("sender_size", help="The size of the sender's set", type=int)
ap.add_argument("receiver_size", help="The size of the receiver's set", type=int)
ap.add_argument("intersection_size", help="The desired size of the intersection", type=int)
ap.add_argument("label_byte_count", nargs='?', help="The number of bytes used for the labels", type=int, default=32)
ap.add_argument("item_byte_count", nargs='?', help="The number of bytes used for the items", type=int, default=8)
args = ap.parse_args()

sender_sz = args.sender_size
recv_sz = args.receiver_size
int_sz = args.intersection_size
label_bc = args.label_byte_count
item_bc = args.item_byte_count # lowest 4 satisfiy sender db 2^20

sender_list = []
letters = string.ascii_lowercase + string.ascii_uppercase
while len(sender_list) < sender_sz:
    item = ''.join(random.choice(letters) for i in range(item_bc))
    label = ''.join(random.choice(letters) for i in range(label_bc))
    sender_list.append((item, label))
print('Done creating sender\'s set')

recv_set = set()
while len(recv_set) < min(int_sz, recv_sz):
    item = random.choice(sender_list)[0]
    recv_set.add(item)

while len(recv_set) < recv_sz:
    item = ''.join(random.choice(letters) for i in range(item_bc))
    recv_set.add(item)
print('Done creating receiver\'s set')

# 生成合并的数据文件名(保存在脚本所在目录)

dataset_file_name = os.path.join(script_dir,"data",f"dataset_{sender_sz}_{recv_sz}_{int_sz}_{label_bc}_{item_bc}.csv")

# 写入合并的文件
with open(dataset_file_name, "w") as dataset_file:
    # 写入 db 参数信息
    dataset_file.write(f"db size {sender_sz} label bytes {label_bc} item bytes {item_bc}\n")
    
    # 写入 db 数据
    for (item, label) in sender_list:
        dataset_file.write(item + (("," + label) if label_bc != 0 else '') + '\n')
    
    # 写入 query 参数信息
    dataset_file.write(f"query size {recv_sz} intersection size {int_sz} item bytes {item_bc}\n")
    
    # 写入 query 数据
    for item in recv_set:
        dataset_file.write(item + '\n')

print(f'Wrote combined dataset to {dataset_file_name}')
