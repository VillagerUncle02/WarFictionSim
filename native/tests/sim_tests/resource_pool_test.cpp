// tests/sim_tests/resource_pool_test.cpp
//
// T049 单元测试：编制资源池与有限分数计算。
// 覆盖（FR-008；宪法第 7/9 条）：
// - 可用力量计算为确定性纯函数（同输入同输出，AI 不可影响输入）；
// - 配属占用扣减与下限、条目顺序保持；
// - 连排级分数扣减（成本 × 数量、剩余不足 INSUFFICIENT_SCORE）；
// - 请求范围约束（SCOPE_VIOLATION 列出越界种类，数量必须为正）。

#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "wfs/sim/resource_pool.h"

namespace {

using wfs::sim::available_force;
using wfs::sim::check_request_scope;
using wfs::sim::deduct_score;
using wfs::sim::EchelonResourcePool;
using wfs::sim::PoolEntryKind;
using wfs::sim::ResourceAllocation;
using wfs::sim::ResourcePoolEntry;

EchelonResourcePool MakePool() {
    EchelonResourcePool pool;
    pool.support_score = 60U;
    pool.support_kinds = {"mortar-82", "artillery-152"};
    pool.entries = {
        ResourcePoolEntry{"squad-mortar-team", PoolEntryKind::kUnit, 2U, 30U},
        ResourcePoolEntry{"squad-atgm-team", PoolEntryKind::kUnit, 3U, 25U},
        ResourcePoolEntry{"artillery-152", PoolEntryKind::kFireSupport, 1U, 40U},
    };
    return pool;
}

}  // namespace

TEST(WfsResourcePoolTest, AvailableForceIsDeterministicAndSubtractsAllocations) {
    const EchelonResourcePool pool = MakePool();
    const std::vector<ResourceAllocation> assigned{{"squad-mortar-team", 1U}, {"squad-atgm-team", 5U}};

    const auto snapshot = available_force(pool, assigned);
    // 确定性纯函数：同输入必然同输出（宪法第 7 条；AI 不参与计算）。
    const auto again = available_force(pool, assigned);
    EXPECT_EQ(snapshot.available, again.available);
    EXPECT_EQ(snapshot.remaining_score, 60U);

    ASSERT_EQ(snapshot.available.size(), 3U);
    EXPECT_EQ(snapshot.available[0].id, "squad-mortar-team");
    EXPECT_EQ(snapshot.available[0].quantity, 1U);  // 2 - 1 占用。
    EXPECT_EQ(snapshot.available[1].id, "squad-atgm-team");
    EXPECT_EQ(snapshot.available[1].quantity, 0U);  // 占用超过池数量，下限 0。
    EXPECT_EQ(snapshot.available[2].id, "artillery-152");
    EXPECT_EQ(snapshot.available[2].quantity, 1U);  // 未占用保持不变。
}

TEST(WfsResourcePoolTest, RequestScopeConstraintsAreExplicit) {
    const EchelonResourcePool pool = MakePool();

    const auto valid = check_request_scope(pool, {"squad-mortar-team"}, 1U);
    EXPECT_TRUE(valid.ok);

    const auto empty = check_request_scope(pool, {}, 1U);
    EXPECT_FALSE(empty.ok);
    EXPECT_NE(empty.error.find("SCOPE_VIOLATION"), std::string::npos);

    const auto zero = check_request_scope(pool, {"squad-mortar-team"}, 0U);
    EXPECT_FALSE(zero.ok);

    // 越界种类必须被列出（确定性顺序 = 请求顺序）。
    const auto out_of_scope = check_request_scope(pool, {"air-support", "tank-125"}, 1U);
    EXPECT_FALSE(out_of_scope.ok);
    EXPECT_NE(out_of_scope.error.find("air-support,tank-125"), std::string::npos);
}

TEST(WfsResourcePoolTest, ScoreDeductionMultipliesQuantityAndFailsWhenExhausted) {
    const EchelonResourcePool pool = MakePool();

    // 30 × 1 = 30，剩余 30。
    const auto first = deduct_score(pool, 60U, {"squad-mortar-team"}, 1U);
    ASSERT_TRUE(first.ok);
    EXPECT_EQ(first.cost, 30U);
    EXPECT_EQ(first.remaining, 30U);

    // 25 × 2 = 50 > 30 → 明确拒绝（用尽即止，FR-008）。
    const auto second = deduct_score(pool, first.remaining, {"squad-atgm-team"}, 2U);
    EXPECT_FALSE(second.ok);
    EXPECT_EQ(second.error, "INSUFFICIENT_SCORE");
    EXPECT_EQ(second.cost, 50U);
    EXPECT_EQ(second.remaining, 30U);

    // 未知种类显式报错（宪法 17：不静默吞错）。
    const auto unknown = deduct_score(pool, 60U, {"air-support"}, 1U);
    EXPECT_FALSE(unknown.ok);
}

TEST(WfsResourcePoolTest, FireSupportKindsAlsoConsumeScore) {
    EchelonResourcePool pool = MakePool();
    // 火力支援种类同样按条目成本扣分（FR-008：按支援种类/数量扣分）。
    const auto result = deduct_score(pool, 60U, {"artillery-152"}, 1U);
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.cost, 40U);
    EXPECT_EQ(result.remaining, 20U);
}
