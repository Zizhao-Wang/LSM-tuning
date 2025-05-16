#!/bin/bash

# --- 请在这里配置您的参数 ---
LOCAL_DIR="请替换为您的本地目录路径"
REMOTE_USER="请替换为您的远程服务器用户名"
REMOTE_IP="请替换为您的远程服务器IP地址" # 例如: "192.168.1.100"
REMOTE_DIR="请替换为您的远程服务器目标目录路径"
NUM_THREADS="请替换为您希望使用的线程数" # 例如: 4
# --- 配置结束 ---

# 检查本地目录是否存在
if [ ! -d "$LOCAL_DIR" ]; then
  echo "错误：本地目录 '$LOCAL_DIR' 不存在。"
  exit 1
fi

# 检查线程数是否为正整数
if ! [[ "$NUM_THREADS" =~ ^[1-9][0-9]*$ ]]; then
  echo "错误：线程数 '$NUM_THREADS' 必须是一个正整数。"
  exit 1
fi

echo "正在从本地目录 '$LOCAL_DIR' 传输文件到远程服务器 '$REMOTE_USER@$REMOTE_IP:$REMOTE_DIR' 使用 $NUM_THREADS 个线程。"

# 获取本地目录下的所有文件（不包括子目录中的文件）
# 如果您也想传输子目录，这里的 find 命令需要调整
cd "$LOCAL_DIR" || exit # 切换到本地目录，如果失败则退出
files_to_transfer=( $(find . -maxdepth 1 -type f -print0 | xargs -0 -I {} basename {}) )
cd - > /dev/null # 返回之前的目录

# 获取文件总数
num_files=${#files_to_transfer[@]}

if [ "$num_files" -eq 0 ]; then
  echo "在 '$LOCAL_DIR' 中没有找到文件可传输。"
  exit 0
fi

echo "总共找到 $num_files 个文件需要传输。"

# 计算每个线程大约处理的文件数量
# 使用 Bash 的算术扩展进行向上取整
files_per_thread=$(( (num_files + NUM_THREADS - 1) / NUM_THREADS ))

echo "每个线程大约处理 $files_per_thread 个文件。"

# 定义传输函数
transfer_files_thread() {
  local thread_id=$1
  shift # 移除第一个参数 (thread_id)
  local files_for_this_thread=("$@")
  local thread_pid=$$ # 获取当前shell进程（即线程）的PID

  if [ ${#files_for_this_thread[@]} -eq 0 ]; then
    echo "线程 $thread_id (PID: $thread_pid): 没有分配到文件，退出。"
    return
  fi

  echo "线程 $thread_id (PID: $thread_pid): 开始传输 ${#files_for_this_thread[@]} 个文件: ${files_for_this_thread[*]}"

  # 构建 scp 命令的文件列表
  # 需要确保文件名中没有空格或特殊字符，或者正确处理它们
  # 为了简单起见，这里假设文件名不包含导致问题的字符
  # 更健壮的方法是逐个 scp 文件，或者将文件名列表传递给 scp 并确保它们被正确引用
  
  local scp_file_list=()
  for file in "${files_for_this_thread[@]}"; do
    scp_file_list+=("./$file") # 相对于 LOCAL_DIR 的路径
  done

  # 执行 scp 命令
  # 使用 -q 参数进行静默传输，可以移除以查看详细输出
  # 使用 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null 可以跳过主机密钥检查（仅用于受信任环境）
  # 请谨慎使用这些ssh选项
  if scp -q "${scp_file_list[@]}" "${REMOTE_USER}@${REMOTE_IP}:${REMOTE_DIR}/"; then
    echo "线程 $thread_id (PID: $thread_pid): 文件传输成功: ${files_for_this_thread[*]}"
  else
    echo "错误：线程 $thread_id (PID: $thread_pid): 文件传输失败: ${files_for_this_thread[*]}"
  fi
}

# 分配文件给每个线程并启动后台进程
file_index=0
pids=() # 用于存储所有后台进程的PID

current_dir_before_loop=$(pwd) # 保存当前目录
cd "$LOCAL_DIR" || { echo "错误: 无法进入目录 $LOCAL_DIR"; exit 1; } # 进入源文件目录

for (( i=0; i<NUM_THREADS; i++ )); do
  # 如果所有文件都已分配，则中断循环
  if [ "$file_index" -ge "$num_files" ]; then
    break
  fi

  # 获取当前线程要处理的文件切片
  start_index=$file_index
  # 确保不会超出文件总数
  end_index=$(( file_index + files_per_thread - 1 ))
  if [ "$end_index" -ge "$num_files" ]; then
    end_index=$(( num_files - 1 ))
  fi

  # 提取文件列表给当前线程
  current_thread_files=()
  for (( j=start_index; j<=end_index; j++ )); do
    current_thread_files+=("${files_to_transfer[j]}")
  done

  # 更新下一个线程的起始文件索引
  file_index=$(( end_index + 1 ))

  # 如果当前线程有文件要处理，则启动后台传输
  if [ ${#current_thread_files[@]} -gt 0 ]; then
    (transfer_files_thread "$((i+1))" "${current_thread_files[@]}") &
    pids+=($!) # 保存最后一个后台进程的PID
    echo "启动线程 $((i+1)) (PID: ${pids[-1]}) 来处理 ${#current_thread_files[@]} 个文件。"
  fi
done

cd "$current_dir_before_loop" # 返回到脚本执行前的目录

# 等待所有后台的 scp 进程完成
echo "等待所有线程完成传输..."
for pid in "${pids[@]}"; do
  wait "$pid"
  status=$?
  if [ $status -ne 0 ]; then
    echo "警告: 线程 PID $pid 以错误码 $status 退出。"
  fi
done

echo "所有文件传输任务已完成！"