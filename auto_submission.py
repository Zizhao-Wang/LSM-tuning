import datetime
from dateutil.relativedelta import relativedelta
from typing import List, Dict, Tuple, Optional
from dataclasses import dataclass
from enum import Enum

# 会议优先级定义
class ConferenceTier(Enum):
    TIER_1 = 1  # OSDI, SOSP
    TIER_2 = 2  # SIGMOD, VLDB
    TIER_3 = 3  # SC, FAST, ASPLOS
    TIER_4 = 4  # ICDE

@dataclass
class Conference:
    """会议数据类"""
    name: str
    deadline: datetime.date
    notification: datetime.date
    areas: List[str]
    features: List[str]
    confidence_match: List[str]
    tier: ConferenceTier
    priority_score: float = 0.0  # 综合优先级分数

# 会议数据配置
CONFERENCE_DATA = [
    Conference(
        name='OSDI 2026',
        deadline=datetime.date(2025, 12, 11),
        notification=datetime.date(2026, 3, 26),
        areas=['Architecture/OS', 'Distributed Systems'],
        features=['High Risk', 'High Prestige', 'Single Round'],
        confidence_match=['High'],
        tier=ConferenceTier.TIER_1
    ),
    Conference(
        name='SOSP 2026',
        deadline=datetime.date(2026, 4, 1),
        notification=datetime.date(2026, 7, 3),
        areas=['Architecture/OS', 'Distributed Systems'],
        features=['High Risk', 'High Prestige', 'Single Round'],
        confidence_match=['High'],
        tier=ConferenceTier.TIER_1
    ),
    Conference(
        name='SIGMOD 2026 (Round 1)',
        deadline=datetime.date(2025, 1, 17),
        notification=datetime.date(2025, 4, 4),
        areas=['Core Database Systems'],
        features=['Revision', 'Medium Risk', 'Multi-round'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_2
    ),
    Conference(
        name='SIGMOD 2026 (Round 2)',
        deadline=datetime.date(2025, 4, 17),
        notification=datetime.date(2025, 7, 4),
        areas=['Core Database Systems'],
        features=['Revision', 'Medium Risk', 'Multi-round'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_2
    ),
    Conference(
        name='SIGMOD 2026 (Round 3)',
        deadline=datetime.date(2025, 7, 17),
        notification=datetime.date(2025, 10, 4),
        areas=['Core Database Systems'],
        features=['Revision', 'Medium Risk', 'Multi-round'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_2
    ),
    Conference(
        name='SIGMOD 2026 (Round 4)',
        deadline=datetime.date(2025, 10, 17),
        notification=datetime.date(2026, 1, 4),
        areas=['Core Database Systems'],
        features=['Revision', 'Medium Risk', 'Multi-round'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_2
    ),
    Conference(
        name='SC 2025',
        deadline=datetime.date(2025, 4, 4),
        notification=datetime.date(2025, 6, 30),
        areas=['Distributed Systems', 'Architecture/OS'],
        features=['Medium Risk', 'HPC Focus'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_3
    ),
    Conference(
        name='FAST 2026 (Spring)',
        deadline=datetime.date(2025, 3, 18),
        notification=datetime.date(2025, 6, 5),
        areas=['Storage/IO'],
        features=['Fast Feedback', 'Low Risk', 'Biannual'],
        confidence_match=['Low', 'Medium'],
        tier=ConferenceTier.TIER_3
    ),
    Conference(
        name='FAST 2026 (Fall)',
        deadline=datetime.date(2025, 9, 16),
        notification=datetime.date(2025, 12, 8),
        areas=['Storage/IO'],
        features=['Fast Feedback', 'Low Risk', 'Biannual'],
        confidence_match=['Low', 'Medium'],
        tier=ConferenceTier.TIER_3
    ),
    Conference(
        name='ASPLOS 2026 (Spring)',
        deadline=datetime.date(2025, 4, 24),
        notification=datetime.date(2025, 7, 31),
        areas=['Architecture/OS', 'Storage/IO'],
        features=['Revision', 'Medium Risk', 'Interdisciplinary'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_3
    ),
    Conference(
        name='ASPLOS 2026 (Summer)',
        deadline=datetime.date(2025, 8, 20),
        notification=datetime.date(2025, 11, 24),
        areas=['Architecture/OS', 'Storage/IO'],
        features=['Revision', 'Medium Risk', 'Interdisciplinary'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_3
    ),
    Conference(
        name='ICDE 2026 (Round 1)',
        deadline=datetime.date(2025, 6, 18),
        notification=datetime.date(2025, 8, 18),
        areas=['Core Database Systems'],
        features=['Revision', 'Medium Risk'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_4
    ),
    Conference(
        name='ICDE 2026 (Round 2)',
        deadline=datetime.date(2025, 10, 27),
        notification=datetime.date(2025, 12, 22),
        areas=['Core Database Systems'],
        features=['Revision', 'Medium Risk'],
        confidence_match=['Medium', 'High'],
        tier=ConferenceTier.TIER_4
    ),
]

# VLDB特殊处理 - 作为Tier 2会议
def generate_vldb_conferences():
    """生成VLDB月度会议列表"""
    vldb_conferences = []
    for year in [2025, 2026]:
        start_month = 1 if year == 2025 else 1
        end_month = 12 if year == 2025 else 6
        for month in range(start_month, end_month + 1):
            deadline = datetime.date(year, month, 1)
            notification = deadline + relativedelta(months=1, days=14)
            vldb_conferences.append(Conference(
                name=f'VLDB {year}-{month:02d}',
                deadline=deadline,
                notification=notification,
                areas=['Core Database Systems'],
                features=['Fast Feedback', 'Monthly', 'Continuous'],
                confidence_match=['Low', 'Medium', 'High'],
                tier=ConferenceTier.TIER_2
            ))
    return vldb_conferences

# 添加VLDB会议到数据集
CONFERENCE_DATA.extend(generate_vldb_conferences())

class SubmissionStrategy:
    """投稿策略优化器"""
    
    def __init__(self):
        self.conferences = CONFERENCE_DATA
        
    def calculate_priority_score(self, conf: Conference, completion_date: datetime.date, 
                               research_area: str, confidence_level: str) -> float:
        """计算会议的综合优先级分数"""
        score = 0.0
        
        # 1. 会议等级权重 (最重要)
        tier_weights = {
            ConferenceTier.TIER_1: 100,
            ConferenceTier.TIER_2: 80,
            ConferenceTier.TIER_3: 60,
            ConferenceTier.TIER_4: 40
        }
        score += tier_weights[conf.tier]
        
        # 2. 领域匹配度
        if research_area in conf.areas:
            score += 20
        
        # 3. 信心等级匹配
        if confidence_level in conf.confidence_match:
            score += 15
        
        # 4. 时间效率 (越早能投稿的加分越多)
        days_to_deadline = (conf.deadline - completion_date).days
        if 14 <= days_to_deadline <= 60:  # 2周到2个月内的最优
            score += 10
        elif 60 < days_to_deadline <= 120:
            score += 5
        
        # 5. 特殊特性加分
        if 'Fast Feedback' in conf.features:
            score += 8  # 快速反馈对迭代有利
        if 'Revision' in conf.features:
            score += 5  # 有修改机会
        if 'Multi-round' in conf.features:
            score += 3  # 多轮次机会
            
        return score
    
    def find_optimal_sequence(self, completion_date: datetime.date, research_area: str, 
                            confidence_level: str, max_submissions: int = 5) -> List[Dict]:
        """
        寻找最优投稿序列
        目标：在一年内投稿尽可能多的高质量会议，同时规避长等待期
        """
        # 为论文准备留出2周缓冲
        earliest_submission = completion_date + relativedelta(weeks=2)
        deadline_limit = completion_date + relativedelta(years=1)
        
        # 计算所有可投会议的优先级分数
        eligible_conferences = []
        for conf in self.conferences:
            if earliest_submission <= conf.deadline <= deadline_limit:
                conf.priority_score = self.calculate_priority_score(
                    conf, completion_date, research_area, confidence_level
                )
                eligible_conferences.append(conf)
        
        # 按优先级排序
        eligible_conferences.sort(key=lambda x: (-x.priority_score, x.deadline))
        
        # 使用动态规划找出最优序列
        submission_plan = self._dynamic_programming_schedule(
            eligible_conferences, earliest_submission, max_submissions
        )
        
        return submission_plan
    
    def _dynamic_programming_schedule(self, conferences: List[Conference], 
                                    start_date: datetime.date, 
                                    max_submissions: int) -> List[Dict]:
        """
        使用动态规划算法找出最优投稿计划
        考虑因素：
        1. 会议优先级
        2. 时间冲突（需要修改时间）
        3. 最大化一年内的投稿机会
        """
        if not conferences:
            return []
        
        # 按截止日期排序
        conferences.sort(key=lambda x: x.deadline)
        n = len(conferences)
        
        # dp[i] = (最大分数, 选择的会议列表)
        dp = [(0, [])] * n
        
        for i in range(n):
            # 选择当前会议的情况
            current_score = conferences[i].priority_score
            current_plan = [conferences[i]]
            
            # 找到之前不冲突的最优方案
            for j in range(i):
                # 假设需要1个月时间准备下一次投稿
                if conferences[j].notification + relativedelta(weeks=4) <= conferences[i].deadline:
                    if dp[j][0] + current_score > current_score:
                        current_score = dp[j][0] + conferences[i].priority_score
                        current_plan = dp[j][1] + [conferences[i]]
            
            # 不选择当前会议的情况
            if i > 0 and dp[i-1][0] > current_score:
                dp[i] = dp[i-1]
            else:
                dp[i] = (current_score, current_plan)
        
        # 转换为结果格式
        optimal_plan = dp[n-1][1][:max_submissions]
        
        result = []
        for i, conf in enumerate(optimal_plan):
            submission_info = {
                'order': i + 1,
                'conference': conf,
                'strategy': self._get_strategy_reason(conf, i, optimal_plan)
            }
            result.append(submission_info)
        
        return result
    
    def _get_strategy_reason(self, conf: Conference, index: int, plan: List[Conference]) -> str:
        """生成投稿策略说明"""
        reasons = []
        
        # 基于会议等级的说明
        if conf.tier == ConferenceTier.TIER_1:
            reasons.append("顶级系统会议，学术影响力极高")
        elif conf.tier == ConferenceTier.TIER_2:
            reasons.append("顶级数据库会议，认可度高")
        elif conf.tier == ConferenceTier.TIER_3:
            reasons.append("优秀专业会议，发表机会较好")
        else:
            reasons.append("知名会议，适合积累经验")
        
        # 基于特性的说明
        if 'Fast Feedback' in conf.features:
            reasons.append("快速反馈周期，便于迭代改进")
        if 'Revision' in conf.features:
            reasons.append("提供修改机会，增加录用概率")
        if 'Monthly' in conf.features:
            reasons.append("月度投稿，时间灵活")
        
        # 基于序列位置的说明
        if index == 0:
            reasons.append("首次投稿，可获得宝贵反馈")
        elif index > 0 and index < len(plan) - 1:
            if plan[index-1].notification + relativedelta(weeks=6) > conf.deadline:
                reasons.append("时间紧凑，需提前准备")
        
        return "；".join(reasons)

def print_optimal_plan(submission_plan: List[Dict]):
    """打印优化后的投稿计划"""
    print("\n" + "="*80)
    print("                    🎯 最优投稿策略方案")
    print("="*80)
    
    if not submission_plan:
        print("\n未找到合适的投稿机会，请调整时间或研究方向。")
        return
    
    total_score = sum(item['conference'].priority_score for item in submission_plan)
    print(f"\n策略概览：共{len(submission_plan)}个投稿机会，总优先级分数：{total_score:.1f}")
    print("-"*80)
    
    for item in submission_plan:
        conf = item['conference']
        print(f"\n📍 第{item['order']}站：{conf.name}")
        print(f"   优先级等级：Tier {conf.tier.value}")
        print(f"   截止日期：{conf.deadline.strftime('%Y年%m月%d日')}")
        print(f"   通知日期：{conf.notification.strftime('%Y年%m月%d日')}")
        print(f"   优先级分数：{conf.priority_score:.1f}")
        print(f"   策略说明：{item['strategy']}")
        
        # 计算与下一个会议的时间间隔
        if item['order'] < len(submission_plan):
            next_conf = submission_plan[item['order']]['conference']
            prep_time = (next_conf.deadline - conf.notification).days
            print(f"   ⏱️ 距下次投稿准备时间：{prep_time}天")
    
    print("\n" + "="*80)
    print("💡 策略建议：")
    print("1. 优先投稿高等级会议，即使难度较大")
    print("2. 利用快速反馈的会议（如VLDB）进行论文迭代")
    print("3. 合理安排时间，确保每次投稿都有充分准备")
    print("4. 保持2-3个会议的投稿节奏，避免过度等待")
    print("="*80)

def get_user_input():
    """获取用户输入"""
    print("\n欢迎使用优化版顶级会议投稿策略规划器！")
    print("本系统将帮您制定最优投稿路线，最大化一年内的发表机会。")
    print("-" * 60)
    
    # 1. 获取项目完成月份
    while True:
        try:
            completion_str = input("\n请输入项目预计完成月份 (格式 YYYY-MM, 例如 2025-03): ")
            completion_date = datetime.datetime.strptime(completion_str + "-01", "%Y-%m-%d").date()
            # 验证日期合理性
            if completion_date < datetime.date.today():
                print("错误：完成日期不能早于今天！")
                continue
            if completion_date > datetime.date.today() + relativedelta(years=2):
                print("错误：完成日期太远，请输入两年内的日期！")
                continue
            break
        except ValueError:
            print("格式错误，请重新输入。")
    
    # 2. 获取研究领域
    areas = {
        '1': 'Core Database Systems',
        '2': 'Storage/IO',
        '3': 'Distributed Systems',
        '4': 'Architecture/OS'
    }
    while True:
        print("\n请选择您的核心研究领域:")
        for k, v in areas.items():
            area_desc = {
                'Core Database Systems': '数据库核心系统、查询优化、事务处理',
                'Storage/IO': '存储系统、文件系统、I/O优化',
                'Distributed Systems': '分布式系统、云计算、容错',
                'Architecture/OS': '计算机体系结构、操作系统、虚拟化'
            }
            print(f"  {k}: {v} ({area_desc[v]})")
        area_choice = input("请输入选项编号: ")
        if area_choice in areas:
            research_area = areas[area_choice]
            break
        else:
            print("无效选项，请重新输入。")
    
    # 3. 获取信心等级
    confidences = {
        '1': 'Low',
        '2': 'Medium',
        '3': 'High'
    }
    while True:
        print("\n请评估您对论文的信心等级:")
        print("  1: 低信心 (创新但需验证，适合先投稿获取反馈)")
        print("  2: 中信心 (工作扎实，但仍有改进空间)")
        print("  3: 高信心 (突破性成果，可以冲击顶会)")
        confidence_choice = input("请输入选项编号: ")
        if confidence_choice in confidences:
            confidence_level = confidences[confidence_choice]
            break
        else:
            print("无效选项，请重新输入。")
    
    # 4. 获取最大投稿数量
    while True:
        try:
            max_submissions = input("\n您希望在一年内最多投稿几次？(建议3-5次): ")
            max_submissions = int(max_submissions)
            if 1 <= max_submissions <= 10:
                break
            else:
                print("请输入1-10之间的数字。")
        except ValueError:
            print("请输入有效数字。")
    
    return completion_date, research_area, confidence_level, max_submissions

def main():
    """主函数"""
    # 获取用户输入
    completion_date, research_area, confidence_level, max_submissions = get_user_input()
    
    # 创建策略优化器
    strategy = SubmissionStrategy()
    
    # 生成最优投稿计划
    print("\n正在分析会议数据，生成最优投稿策略...")
    submission_plan = strategy.find_optimal_sequence(
        completion_date, research_area, confidence_level, max_submissions
    )
    
    # 打印结果
    print_optimal_plan(submission_plan)
    
    # 提供额外建议
    print("\n📌 额外建议：")
    if confidence_level == 'Low':
        print("- 建议先投稿VLDB获取快速反馈")
        print("- 根据反馈迭代改进后再投高级别会议")
    elif confidence_level == 'Medium':
        print("- 可以同时准备Tier 2和Tier 3会议")
        print("- 利用有revision机会的会议提高录用率")
    else:
        print("- 直接瞄准OSDI/SOSP等顶级会议")
        print("- 准备好备选方案以防万一")

if __name__ == "__main__":
    main()