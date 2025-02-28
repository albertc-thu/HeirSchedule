def is_prime(n):
    """检查一个数是否为素数"""
    if n <= 1:
        return False
    if n <= 3:
        return True
    if n % 2 == 0 or n % 3 == 0:
        return False
    i = 5
    while i * i <= n:
        if n % i == 0 or n % (i + 2) == 0:
            return False
        i += 6
    return True

def is_prime_power(n):
    """检查一个数是否为素数幂"""
    if n <= 1:
        return False
    # 找到n的所有素因子
    i = 2
    factors = set()
    temp_n = n
    while i * i <= temp_n:
        while temp_n % i == 0:
            factors.add(i)
            temp_n //= i
        i += 1
    if temp_n > 1:
        factors.add(temp_n)
    
    # 如果只有一个素因子，就是素数幂
    return len(factors) == 1

def construct_array(n):
    """构造满足条件的n个排列"""
    if not is_prime_power(n):
        return None, f"目前只支持n为素数幂的情况 (n = {n} 不是素数幂)"
    
    # 创建有限域GF(n)的加法表和乘法表
    # 对于素数n，我们使用模n的整数算术
    # 对于素数幂，需要更复杂的有限域构造，这里简化处理
    
    # 生成n个排列
    permutations = []
    
    # 第一个排列是自然顺序
    first_perm = list(range(1, n*n + 1))
    permutations.append(first_perm)
    
    if n == 2:
        # n=2的特殊情况
        second_perm = [1, 3, 2, 4]
        permutations.append(second_perm)
        return permutations, "成功构造排列"
        
    elif n == 3:
        # n=3的特殊情况，直接给出已知正确的构造
        second_perm = [1, 4, 7, 2, 5, 8, 3, 6, 9]
        third_perm = [1, 6, 8, 3, 4, 9, 2, 5, 7]
        permutations.append(second_perm)
        permutations.append(third_perm)
        return permutations, "成功构造排列"
        
    elif is_prime(n):
        # 对于素数n，使用射影平面构造
        # 构造n-1个额外的排列
        for a in range(1, n):
            perm = []
            for i in range(n):
                module = []
                for j in range(n):
                    # 计算元素在第一个排列中的位置
                    val = ((a * i + j) % n) * n + i + 1
                    module.append(val)
                perm.extend(module)
            permutations.append(perm)
        
        return permutations, "成功构造排列"
        
    else:
        return None, "目前只实现了n为2, 3或其他素数的情况"

def verify_solution(permutations, n):
    """验证构造的排列是否满足条件"""
    if not permutations:
        return False
    
    # 检查是否有n个排列
    if len(permutations) != n:
        print(f"排列数量不正确: 期望{n}个, 实际{len(permutations)}个")
        return False
    
    # 检查每个排列是否包含所有n^2个数
    for i, perm in enumerate(permutations):
        if len(perm) != n*n:
            print(f"排列{i+1}长度不正确: 期望{n*n}, 实际{len(perm)}")
            return False
        if set(perm) != set(range(1, n*n + 1)):
            print(f"排列{i+1}不包含所有数字1到{n*n}")
            return False
    
    # 检查任意两个不同排列中的任意两个模块是否恰好有一个共同元素
    for i in range(n):
        for j in range(i+1, n):  # 不同排列
            for x in range(n):  # 第一个排列的模块
                for y in range(n):  # 第二个排列的模块
                    module_i = set(permutations[i][x*n:(x+1)*n])
                    module_j = set(permutations[j][y*n:(y+1)*n])
                    intersection = module_i.intersection(module_j)
                    if len(intersection) != 1:
                        print(f"不满足条件: 排列{i+1}的模块{x+1}与排列{j+1}的模块{y+1}共有{len(intersection)}个元素")
                        return False
    
    return True

def display_permutations(permutations, n):
    """以易读的方式显示排列和模块"""
    for i, perm in enumerate(permutations):
        print(f"排列 {i+1}:")
        for x in range(n):
            module = perm[x*n:(x+1)*n]
            print(f"  模块 {x+1}: {module}")
        print()

def main():
    # 测试几个特定的n值
    for n in [2, 3, 4, 5, 7, 8, 9]:
        print(f"\n测试 n = {n}:")
        permutations, message = construct_array(n)
        print(message)
        
        if permutations:
            is_valid = verify_solution(permutations, n)
            print(f"验证结果: {'通过' if is_valid else '失败'}")
            
            if n <= 5:  # 对于较小的n值，显示详细信息
                display_permutations(permutations, n)

if __name__ == "__main__":
    main()
