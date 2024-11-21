import re
import matplotlib.pyplot as plt
import numpy as np
from fpdf import FPDF

data_file = "data.txt"  
ping_data = []

# 正则表达式解析每行数据
pattern = re.compile(r"icmp_seq=(\d+).*time=([\d.]+) ms")

last_seq = None  # 用于检测丢包
with open(data_file, "r") as file:
    for line in file:
        line = line.strip()
        match = pattern.search(line)
        if match:
            seq = int(match.group(1))
            rtt = float(match.group(2))
            ping_data.append((seq, rtt))
            #print(f"解析成功: icmp_seq={seq}, RTT={rtt} ms") 

            # 丢包检测逻辑
            if last_seq is not None and seq != last_seq + 1:
               # print(f"丢包检测: icmp_seq={seq} 丢包")  # 丢包检测
                # 标记为丢包
                for missed_seq in range(last_seq + 1, seq):
                    ping_data.append((missed_seq, None))  # None表示丢包
            last_seq = seq

ping_data.sort(key=lambda x: x[0])  # 按序号排序
#print(f"数据排序后: {ping_data[:10]}")  

# 总发送、接收
sent = len(ping_data)
received = sum(1 for _, rtt in ping_data if rtt is not None)
delivery_rate = received / sent
#print(f"总发送包数: {sent}, 总接收包数: {received}, 发送成功率: {delivery_rate:.2%}")  # 显示发送成功率

# 连续成功回复和丢包
max_success, max_loss, current_success, current_loss = 0, 0, 0, 0
for i in range(1, len(ping_data)):
    _, rtt = ping_data[i - 1]
    if rtt is not None:  # 当前包是成功的
        current_success += 1
        current_loss = 0
    else:  # 当前包是丢包的
        current_loss += 1
        current_success = 0
    max_success = max(max_success, current_success)
    max_loss = max(max_loss, current_loss)

#print(f"最长连续成功包数: {max_success}, 最长连续丢包数: {max_loss}")  # 显示最大连续成功和丢包

# 条件概率
success_after_success, success_after_loss = 0, 0
total_success_after_success, total_success_after_loss = 0, 0
for i in range(1, len(ping_data)):
    current, next_ping = ping_data[i - 1][1], ping_data[i][1]
    # 当前包是成功的
    if current is not None:
        total_success_after_success += 1
        if next_ping is not None:
            success_after_success += 1
    # 当前包是丢包的
    elif current is None:
        total_success_after_loss += 1
        if next_ping is not None:
            success_after_loss += 1

p_success_given_success = (success_after_success / total_success_after_success 
                           if total_success_after_success > 0 else 0)
p_success_given_loss = (success_after_loss / total_success_after_loss 
                        if total_success_after_loss > 0 else 0)

#print(f"P(Success | Success): {p_success_given_success:.2%}, P(Success | Loss): {p_success_given_loss:.2%}") 

# 最小和最大 RTT
rtt_values = [rtt for _, rtt in ping_data if rtt is not None]
min_rtt = min(rtt_values)
max_rtt = max(rtt_values)

#print(f"最小 RTT: {min_rtt} ms, 最大 RTT: {max_rtt} ms")  


# RTT 随时间变化图
times = [seq * 0.2 for seq, rtt in ping_data if rtt is not None]  
rtts = [rtt for _, rtt in ping_data if rtt is not None]

plt.figure(figsize=(10, 5))
plt.plot(times, rtts, label="RTT")
plt.xlabel("Time (s)")
plt.ylabel("RTT (ms)")
plt.title("RTT Over Time")
plt.legend()
plt.grid()
plt.savefig("rtt_over_time.png")
plt.close()

# RTT分布的直方图或累积分布函数
plt.figure(figsize=(10, 5))
plt.hist(rtt_values, bins=50, density=True, alpha=0.6, label="Histogram")
cdf = np.cumsum(np.histogram(rtt_values, bins=50, density=True)[0])
plt.plot(np.linspace(min(rtt_values), max(rtt_values), len(cdf)), cdf, label="CDF")
plt.xlabel("RTT (ms)")
plt.ylabel("Frequency / Cumulative Probability")
plt.title("RTT Distribution")
plt.legend()
plt.grid()
plt.savefig("rtt_histogram_cdf.png")
plt.close()

# RTT(N)和RTT(N+1)相关性图表
rtt_pairs = [(rtt, ping_data[i + 1][1]) for i, (_, rtt) in enumerate(ping_data[:-1]) if rtt is not None and ping_data[i + 1][1] is not None]
x, y = zip(*rtt_pairs)

plt.figure(figsize=(8, 8))
plt.scatter(x, y, alpha=0.6)
plt.xlabel("RTT(N) (ms)")
plt.ylabel("RTT(N+1) (ms)")
plt.title(" Correlation Between RTT(N) and RTT(N+1)")
plt.grid()
plt.savefig("rtt_correlation.png")
plt.close()

# PDF 生成 #

pdf = FPDF()
pdf.set_auto_page_break(auto=True, margin=15)
pdf.add_page()
pdf.set_font("Arial", size=12)

pdf.cell(200, 10, txt="Ping Analysis Report", ln=True, align='C')
pdf.ln(10)

pdf.multi_cell(0, 10, txt=(f"1. Overall Delivery Rate: {delivery_rate:.2%}\n"
                           f"2. Longest Consecutive Successful Pings: {max_success}\n"
                           f"3. Longest Burst of Packet Losses: {max_loss}\n"
                           f"4. Conditional Delivery Rate:\n"
                           f"   - P(Success): {p_success_given_success:.2%}\n"
                           f"   - P(Loss): {p_success_given_loss:.2%}\n"
                           f"5. Minimum RTT: {min_rtt} ms\n"
                           f"6. Maximum RTT: {max_rtt} ms\n"))
pdf.ln(10)

pdf.cell(0, 10, txt="RTT Over Time", ln=True, align='L')
pdf.image("rtt_over_time.png", x=10, y=None, w=180)

pdf.add_page()
pdf.cell(0, 10, txt="RTT Histogram and CDF", ln=True, align='L')
pdf.image("rtt_histogram_cdf.png", x=10, y=None, w=180)

pdf.add_page()
pdf.cell(0, 10, txt="RTT Correlation", ln=True, align='L')
pdf.image("rtt_correlation.png", x=10, y=None, w=180)

pdf.output("ping_analysis_report.pdf")
