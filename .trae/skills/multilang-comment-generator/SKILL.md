---
name: "multilang-comment-generator"
description: "为已有代码自动添加简体中文头部注释和行内注释，支持 Vue3+JavaScript、C++、Python。Invoke when user provides code files/snippets and asks to add Chinese comments, annotations, or doc comments to existing code."
---

# 多语言智能注释生成器

为已有代码补充/完善简体中文注释，**只添加注释，不修改原有代码**。

## 适用场景

- 用户提交了完整的代码文件或代码片段，希望补全中文注释
- 用户希望按照 Google / JSDoc / Doxygen 规范生成函数级文档注释
- 用户希望对关键逻辑、复杂算法、边界条件添加行内注释

## 支持语言

| 语言 | 注释规范 | 文件后缀 |
|------|----------|----------|
| Python | Google Style（简化版） | `.py` |
| JavaScript / Vue3 | JSDoc Style（简化版） | `.js`、`.ts`、`.vue` |
| C++ | Doxygen Style（简化版） | `.cpp`、`.cc`、`.h`、`.hpp` |

## 工作流程

1. **识别语言**：通过文件扩展名或代码特征判断语言类型
2. **过滤代码**：检测并跳过测试代码、自动生成代码、第三方库代码
3. **解析结构**：提取所有函数/方法定义，分析参数、返回值、逻辑复杂度
4. **生成头部注释**：为每个函数添加符合规范的中文文档注释
5. **生成行内注释**：为关键代码块添加简洁的行内注释
6. **输出报告**：给出注释添加统计

## 排除规则（必须跳过）

### 测试代码
- JavaScript：`*.test.js`、`*.spec.js`、`*.test.ts`，目录 `__tests__/`、`test/`、`tests/`
- Python：`test_*.py`、`*_test.py`，目录 `tests/`、`test/`
- C++：`*_test.cpp`、`*_test.cc`、`test_*.cpp`，目录 `tests/`、`unittest/`

### 自动生成代码
- 文件头部包含 `@generated`、`AUTO-GENERATED`、`自动生成` 标记
- 文件路径包含 `generated/`、`gen/`、`pb/`（protobuf 生成）
- 文件被 `/* istanbul ignore */` 或 `// istanbul ignore next` 标记
- 文件被 `/* @skip-comment */` 自定义标记包裹

## 注释规范

### Python - Google Style

```python
def function_name(param1, param2=10):
    """
    函数功能的简要描述，使用中文。

    如果有需要说明的细节，可以在这里展开描述。

    Args:
        param1 (str): 参数1的含义和用途
        param2 (int, optional): 参数2的含义，默认值为10

    Returns:
        bool: 返回值含义，True表示成功，False表示失败

    Raises:
        ValueError: 当参数不合法时抛出
    """
    pass
```

### JavaScript / Vue3 - JSDoc Style

```javascript
/**
 * 函数功能的简要描述，使用中文。
 *
 * 如果有需要说明的细节，可以在这里展开描述。
 *
 * @param {string} param1 - 参数1的含义和用途
 * @param {number} [param2=10] - 参数2的含义，默认值为10
 * @returns {boolean} 返回值含义，true表示成功，false表示失败
 * @throws {Error} 当参数不合法时抛出错误
 */
function functionName(param1, param2 = 10) {
    // 关键逻辑说明
    if (!param1) {
        throw new Error('参数不合法');
    }
    return true;
}
```

### C++ - Doxygen Style

```cpp
/**
 * @brief 函数功能的简要描述，使用中文。
 *
 * 如果有需要说明的细节，可以在这里展开描述。
 *
 * @param param1 参数1的含义和用途，类型为std::string
 * @param param2 参数2的含义，默认值为10，类型为int
 * @return bool 返回值含义，true表示成功，false表示失败
 * @throws std::invalid_argument 当参数不合法时抛出
 */
bool functionName(const std::string& param1, int param2 = 10) {
    // 关键逻辑说明
    if (param1.empty()) {
        throw std::invalid_argument("参数不合法");
    }
    return true;
}
```

## 行内注释原则

**必须添加行内注释的场景：**

| 类型 | 示例 | 注释内容 |
|------|------|----------|
| 复杂条件判断 | `if (a > b && c < d || e == f)` | 说明判断的业务逻辑含义 |
| 算法核心步骤 | 排序、搜索、递归 | 说明算法思路和关键步骤 |
| 边界值处理 | `if (index === 0)`、`if (length === 0)` | 说明为什么需要特殊处理 |
| 性能优化代码 | 缓存、防抖、节流、懒加载 | 说明优化目的 |
| 非显而易见的逻辑 | 位运算、魔法数字、特殊计算 | 解释为什么要这样写 |
| 重要状态变更 | 数据持久化、全局状态修改 | 说明变更的影响 |
| 异步操作 | Promise、async/await、回调 | 说明执行顺序和依赖关系 |

**避免的注释：**

```javascript
// ❌ 不好：过于简单、没有意义
const result = data.map(item => {
    // 使用map遍历
    if (!item.id || !item.value) return null; // 检查id和value
    return { id: item.id, time: item.timestamp / 1000 };
});

// ✅ 好：解释业务逻辑和原因
const result = data.map(item => {
    // 过滤掉无效数据，避免后续计算报错
    if (!item.id || !item.value) return null;
    // 将数值单位从毫秒转换为秒，保持与后端接口一致
    return { id: item.id, time: item.timestamp / 1000 };
}).filter(Boolean);
```

## 强制约束

1. **只添加注释**：绝对不修改原有代码的逻辑、格式、变量名
2. **中文优先**：所有注释使用简体中文
3. **规范一致**：同一语言的注释风格保持一致
4. **避免冗余**：不为 `console.log`、简单变量赋值等显而易见的代码添加注释
5. **保持简洁**：行内注释应该简短精准
6. **行内注释格式**：JavaScript/C++ 使用 `//`，Python 使用 `#`
7. **注释位置**：行内注释与代码在同一行或上一行
8. **单行长度**：单行注释最大 120 字符，超出则换行

## 输出报告

完成注释添加后，输出以下报告：

```markdown
## 📊 注释生成报告

### 处理统计
- 总文件数：N
- 总函数/方法数：N
- 添加头部注释：N 个
- 添加行内注释：N 处
- 跳过代码：N 个

### 注释分布
| 语言 | 函数数 | 行内注释数 |
|------|--------|------------|
| Python | N | N |
| JavaScript | N | N |
| C++ | N | N |

### 跳过的内容
- 文件路径 / 跳过原因

### 提示
- 所有注释均使用中文
- 原有代码未被修改
- 建议人工复核复杂逻辑部分的注释准确性
```

## 质量检查清单

生成注释后必须确认：

- [ ] 每个导出函数都有头部注释
- [ ] 所有参数都有类型和含义说明
- [ ] 返回值有明确的类型和含义说明
- [ ] 行内注释使用 `//`（Python 使用 `#`）
- [ ] 注释与代码在同一行或上一行
- [ ] 注释语法符合对应语言的规范
- [ ] 关键逻辑（条件、循环、异步）都有注释
- [ ] 没有修改任何原有代码
- [ ] 没有使用英文注释
