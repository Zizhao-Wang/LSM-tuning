import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
from scipy.stats import uniform
key_size = 16
val_size = 128

def generate_uniform_data(low=0, high=1, size=1000, keys_file_name=""):
    """
    使用 scipy.stats.uniform 生成均匀分布的数据
    :param low: 数据的最小值，默认为 0
    :param high: 数据的最大值，默认为 1
    :param size: 生成的数据点个数，默认为 1000
    :return: 均匀分布的数组
    """
    
    # 使用 scipy.stats.uniform 来生成数据
    keys = uniform.rvs(loc=low, scale=high-low, size=size)
    keys = np.floor(keys).astype(int)  # 取整生成整数键

    data = pd.DataFrame({'Key': keys})
    # data.to_csv(keys_file_name, index=False)
    print(f"Data for uniform distribution keys written to {keys_file_name}")

    key_counts = data['Key'].value_counts().sort_index()
    print("Top 10 most frequent keys:")
    print(key_counts.head(10))
    # 可视化每个key的出现次数
    plt.bar(key_counts.index, key_counts.values, alpha=0.6, color='g')
    plt.title(f"Key Frequency Distribution from {low} to {high}")
    plt.xlabel("Key")
    plt.ylabel("Frequency")
    plt.savefig(f'uniform_key_frequency_{key_size}_{val_size}.png')
    plt.show()

    return keys

# 使用示例
low = 1  # 数据的最小值
high = 1000000000  # 数据的最大值
size = 1000000000  # 数据点的数量

uniform_file_name = f'/mnt/workloads/uniform_read_keys{key_size}_value{val_size}_1B.csv'
generate_uniform_data(low, high, size, uniform_file_name)
