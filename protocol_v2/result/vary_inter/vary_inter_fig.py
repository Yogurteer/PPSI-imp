import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import os

# Get the directory where the script is located
script_dir = os.path.dirname(os.path.abspath(__file__))

# Read the CSV file from script directory
csv_path = os.path.join(script_dir, 'vary_inter_summary.csv')
df = pd.read_csv(csv_path)

# Create figure with 3 subplots
fig, axes = plt.subplots(1, 3, figsize=(18, 5))

# Extract data
intersection_sizes = df['Intersection_Size'].values
offline_time = df['Total_Offline(s)'].values
online_time = df['Total_Online(s)'].values
communication = df['Communication(MB)'].values

# Set style
plt.style.use('seaborn-v0_8-darkgrid')
colors = ['#2E86AB', '#A23B72', '#F18F01']

# Custom x-axis labels
x_labels = ['1', '10%', '20%', '30%', '40%', '50%', '60%', '70%', '80%', '90%', '100%']
x_positions = range(len(intersection_sizes))

# Plot 1: Offline Time
axes[0].plot(x_positions, offline_time, marker='o', linewidth=2.5, 
             markersize=8, color=colors[0], label='Offline Time')
axes[0].set_xlabel('Intersection Size', fontsize=14, fontweight='bold')
axes[0].set_ylabel('Offline Time (s)', fontsize=14, fontweight='bold')
axes[0].set_title('Offline Time vs Intersection Size', fontsize=15, fontweight='bold')
axes[0].set_ylim(0, 600)
axes[0].set_yticks(np.arange(0, 601, 100))
axes[0].set_xticks(x_positions)
axes[0].set_xticklabels(x_labels, rotation=45)
axes[0].grid(True, alpha=0.3)
axes[0].tick_params(labelsize=12)

# Plot 2: Online Time
axes[1].plot(x_positions, online_time, marker='s', linewidth=2.5, 
             markersize=8, color=colors[1], label='Online Time')
axes[1].set_xlabel('Intersection Size', fontsize=14, fontweight='bold')
axes[1].set_ylabel('Online Time (s)', fontsize=14, fontweight='bold')
axes[1].set_title('Online Time vs Intersection Size', fontsize=15, fontweight='bold')
axes[1].set_xticks(x_positions)
axes[1].set_xticklabels(x_labels, rotation=45)
axes[1].grid(True, alpha=0.3)
axes[1].tick_params(labelsize=12)

# Plot 3: Communication
axes[2].plot(x_positions, communication, marker='^', linewidth=2.5, 
             markersize=8, color=colors[2], label='Communication')
axes[2].set_xlabel('Intersection Size', fontsize=14, fontweight='bold')
axes[2].set_ylabel('Communication (MB)', fontsize=14, fontweight='bold')
axes[2].set_title('Communication vs Intersection Size', fontsize=15, fontweight='bold')
axes[2].set_xticks(x_positions)
axes[2].set_xticklabels(x_labels, rotation=45)
axes[2].grid(True, alpha=0.3)
axes[2].tick_params(labelsize=12)

# Adjust layout
plt.tight_layout()

# Save figure to script directory
output_png = os.path.join(script_dir, 'vary_inter_performance.png')
output_pdf = os.path.join(script_dir, 'vary_inter_performance.pdf')
plt.savefig(output_png, dpi=300, bbox_inches='tight')
# plt.savefig(output_pdf, bbox_inches='tight')
print(f"Figures saved as:\n  {output_png}")

# Show the plot
plt.show()
