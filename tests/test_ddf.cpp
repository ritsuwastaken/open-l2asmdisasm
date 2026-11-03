#include "support.h"

using namespace l2asmdisasm;

TEST(Ddf, RepeatedGlobals)
{
    const auto repeated = parse_ddf("FS=\"|\"; HEADER=1; FS=\",\"; HEADER=0; { UINT a; }");
    EXPECT_EQ(repeated.separator, ",");
    EXPECT_FALSE(repeated.header);
    EXPECT_REJECTS(parse_ddf("FS=\"\"; { UINT a; }"), "FS");
}

TEST(Ddf, RejectsInvalid)
{
    for (const auto ddf : {
             "{}", "{ UNKNOWN mesh; }", "{ UINT x[n]; }", "{ UINT x[0]; }", "{ UINT x; ORDER=1; }", "{ UINT x; } trailing",
             "/* unterminated", "FS=\"unterminated", "HEADER=3; {UINT a;}", "RECCNT=-2; {UINT a;}", "MAGIC=-1; {UINT a;}", "BOGUS=1; {UINT a;}",
             // FILLER and ENBBY
             "{ FILLER x{0}; }", "{ FILLER x{3}[2]; }", "{ UINT x; ENBBY=[]; }", "{ UINT x; ENBBY=[(later,1)]; UINT later; }",
             "{ ASCF s; UINT x; ENBBY=[(s,1)]; }", "{ UINT n; UINT x; ENBBY=[(n:4294967296,1)]; }",
             // ORD and SKIPIF
             "{ UINT a; ORD=1; ORD=2; }", "{ UINT a; ORD=2147483648; }", "{ ASCF s; SKIPIF=[(0,0)]; }", "{ CNTR n; SKIPIF=[(0,0)]; }",
             "{ FILLER f{2}; SKIPIF=[(0,0)]; }", "{ UINT a; SKIPIF=[]; }", "{ UINT a; SKIPIF=[(-1,0)]; }", "{ UINT a; SKIPIF=[(2 .. 1,0)]; }",
             "{ UINT a; SKIPIF=[(0,3 .. 2)]; }", "{ UINT a; SKIPIF=[(0,0.5)]; }", "{ FLOAT a; SKIPIF=[(0,nan)]; }",
             // counters and sizes
             "{ FLOAT n; UINT x[n]; }", "{ MAT n; UINT x[n]; }", "{ MAT x; SOFTT=1; }", "{ UINT x; SOFT=1; }", "{ MTX x; SOFTM=100001; }"})
        EXPECT_REJECTS(parse_ddf(ddf), "DDF ") << ddf;
}

TEST(Ddf, RejectsInvalidGroups)
{
    for (const auto ddf : {"{ {} g; }", "{ { UINT a; } g{2}; }", "{ GROUP g; }", "{ { UINT a; ORD=1; } g; }", "{ { UINT a; SKIPIF=[(0,0)]; } g; }",
                           "{ { UINT a[later]; UINT later; } g; }", "{ UINT n; { UINT a; } g[n]; UINT x; ENBBY=[(a,1)]; }", "{ { UINT a; } g[0]; }"})
        EXPECT_REJECTS(parse_ddf(ddf), "DDF ") << ddf;
}
