#include "PinyinHelper.h"
#include <cwctype>
#include <algorithm>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace
{
    // CJK Unified Ideographs Unicode ranges for Pinyin Initials & Full Pinyin
    // Simplified mapping covering standard CJK characters (0x4E00 - 0x9FA5)
    struct PinyinEntry
    {
        wchar_t ch;
        const wchar_t* initial;
        const wchar_t* full;
    };

    // Helper to get pinyin for a single character
    bool GetCharPinyin(wchar_t ch, std::wstring& outInitial, std::wstring& outFull)
    {
        // English / Digit / ASCII
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'))
        {
            wchar_t lower = static_cast<wchar_t>(towlower(ch));
            outInitial.assign(1, lower);
            outFull.assign(1, lower);
            return true;
        }

        // Common CJK ideograph lookup or boundary check
        if (ch >= 0x4E00 && ch <= 0x9FA5)
        {
            // First letter lookup based on GB2312/Unicode CJK ranges
            static const struct Boundary { wchar_t start; const wchar_t* initial; const wchar_t* full; } boundaries[] = {
                { 0x4E00, L"a", L"a" }, { 0x4E09, L"b", L"ba" }, { 0x4E0A, L"c", L"cang" },
                { 0x4E0D, L"b", L"bu" }, { 0x4E10, L"b", L"bing" }, { 0x4E25, L"y", L"yan" },
                { 0x4E2D, L"z", L"zhong" }, { 0x4E3B, L"z", L"zhu" }, { 0x4E48, L"m", L"me" },
                { 0x4E5D, L"j", L"jiu" }, { 0x4E66, L"s", L"shu" }, { 0x4E86, L"l", L"le" },
                { 0x4E8B, L"s", L"shi" }, { 0x4E8C, L"e", L"er" }, { 0x4E94, L"w", L"wu" },
                { 0x4EA4, L"j", L"jiao" }, { 0x4EBA, L"r", L"ren" }, { 0x4EC0, L"s", L"shen" },
                { 0x4ED6, L"t", L"ta" }, { 0x4EE4, L"l", L"ling" }, { 0x4EF6, L"j", L"jian" },
                { 0x4F0F, L"f", L"fu" }, { 0x4F1A, L"h", L"hui" }, { 0x4F4D, L"w", L"wei" },
                { 0x4F5C, L"z", L"zuo" }, { 0x4F60, L"n", L"ni" }, { 0x4F7F, L"s", L"shi" },
                { 0x4FBF, L"b", L"bian" }, { 0x4FE1, L"x", L"xin" }, { 0x4FEE, L"x", L"xiu" },
                { 0x50CF, L"x", L"xiang" }, { 0x5165, L"r", L"ru" }, { 0x5166, L"l", L"liu" },
                { 0x5173, L"g", L"guan" }, { 0x5177, L"g", L"ju" }, { 0x5185, L"n", L"nei" },
                { 0x51FA, L"c", L"chu" }, { 0x5206, L"f", L"fen" }, { 0x5207, L"q", L"qie" },
                { 0x521D, L"c", L"chu" }, { 0x5220, L"s", L"shan" }, { 0x524D, L"q", L"qian" },
                { 0x52A0, L"j", L"jia" }, { 0x52A8, L"d", L"dong" }, { 0x5305, L"b", L"bao" },
                { 0x5316, L"h", L"hua" }, { 0x533A, L"q", L"qu" }, { 0x5341, L"s", L"shi" },
                { 0x5355, L"d", L"dan" }, { 0x5357, L"n", L"nan" }, { 0x535A, L"b", L"bo" },
                { 0x5361, L"k", L"ka" }, { 0x5370, L"y", L"yin" }, { 0x5371, L"w", L"wei" },
                { 0x5373, L"j", L"ji" }, { 0x5386, L"l", L"li" }, { 0x538B, L"y", L"ya" },
                { 0x539F, L"y", L"yuan" }, { 0x53BB, L"q", L"qu" }, { 0x53C2, L"c", L"can" },
                { 0x53C8, L"y", L"you" }, { 0x53CC, L"s", L"shuang" }, { 0x53D1, L"f", L"fa" },
                { 0x53D6, L"q", L"qu" }, { 0x53D7, L"s", L"shou" }, { 0x53D8, L"b", L"bian" },
                { 0x53E3, L"k", L"kou" }, { 0x53EB, L"j", L"jiao" }, { 0x53EF, L"k", L"ke" },
                { 0x53F0, L"t", L"tai" }, { 0x53F1, L"y", L"ye" }, { 0x53F7, L"h", L"hao" },
                { 0x5404, L"g", L"ge" }, { 0x5408, L"h", L"he" }, { 0x5409, L"j", L"ji" }, { 0x540C, L"t", L"tong" },
                { 0x540D, L"m", L"ming" }, { 0x540E, L"h", L"hou" }, { 0x5411, L"x", L"xiang" },
                { 0x5448, L"c", L"cheng" }, { 0x544A, L"g", L"gao" }, { 0x5458, L"y", L"yuan" },
                { 0x548C, L"h", L"he" }, { 0x54C1, L"p", L"pin" }, { 0x54CD, L"x", L"xiang" },
                { 0x55EE, L"j", L"ji" }, { 0x56FE, L"t", L"tu" }, { 0x5706, L"y", L"yuan" },
                { 0x5708, L"q", L"quan" }, { 0x5728, L"z", L"zai" }, { 0x5730, L"d", L"di" },
                { 0x573A, L"c", L"chang" }, { 0x57FA, L"j", L"ji" }, { 0x58EB, L"s", L"shi" },
                { 0x5904, L"c", L"chu" }, { 0x5907, L"b", L"bei" }, { 0x591A, L"d", L"duo" },
                { 0x5927, L"d", L"da" }, { 0x5929, L"t", L"tian" }, { 0x592A, L"t", L"tai" },
                { 0x5934, L"t", L"tou" }, { 0x5973, L"n", L"nv" }, { 0x597D, L"h", L"hao" },
                { 0x5B57, L"z", L"zi" }, { 0x5B58, L"c", L"cun" }, { 0x5B66, L"x", L"xue" },
                { 0x5B89, L"a", L"an" }, { 0x5B9A, L"d", L"ding" }, { 0x5BA2, L"k", L"ke" },
                { 0x5BB6, L"j", L"jia" }, { 0x5BB9, L"r", L"rong" }, { 0x5BD6, L"m", L"mi" },
                { 0x5BD9, L"d", L"dui" }, { 0x5BFB, L"x", L"xun" }, { 0x5BFC, L"d", L"dao" }, { 0x5C06, L"j", L"jiang" },
                { 0x5C0F, L"x", L"xiao" }, { 0x5C11, L"s", L"shao" }, { 0x5C31, L"j", L"jiu" },
                { 0x5C42, L"c", L"ceng" }, { 0x5C4E, L"s", L"shi" }, { 0x5C71, L"s", L"shan" },
                { 0x5DE5, L"g", L"gong" }, { 0x5DDF, L"z", L"zuo" }, { 0x5E2E, L"b", L"bang" },
                { 0x5E38, L"c", L"chang" }, { 0x5E72, L"g", L"gan" }, { 0x5E77, L"b", L"bing" },
                { 0x5E74, L"n", L"nian" }, { 0x5E76, L"b", L"bing" }, { 0x5E94, L"y", L"ying" },
                { 0x5E95, L"d", L"di" }, { 0x5EA6, L"d", L"du" }, { 0x5EFA, L"j", L"jian" },
                { 0x5F00, L"k", L"kai" }, { 0x5F0F, L"s", L"shi" }, { 0x5F15, L"y", L"yin" },
                { 0x5F3A, L"q", L"qiang" }, { 0x5F62, L"x", L"xing" }, { 0x5F69, L"c", L"cai" },
                { 0x5F71, L"y", L"ying" }, { 0x5FBE, L"w", L"wang" }, { 0x5FCD, L"h", L"hou" },
                { 0x5FA8, L"d", L"de" }, { 0x5FC3, L"x", L"xin" }, { 0x5BF1, L"b", L"bili" },
                { 0x606F, L"x", L"xi" }, { 0x60A3, L"h", L"huan" }, { 0x60C5, L"q", L"qing" },
                { 0x60F3, L"x", L"xiang" }, { 0x610F, L"y", L"yi" }, { 0x611F, L"g", L"gan" },
                { 0x6210, L"c", L"cheng" }, { 0x6216, L"h", L"huo" }, { 0x6218, L"z", L"zhan" },
                { 0x6237, L"h", L"hu" }, { 0x623F, L"f", L"fang" }, { 0x624B, L"s", L"shou" },
                { 0x624D, L"c", L"cai" }, { 0x6253, L"d", L"da" }, { 0x626B, L"s", L"sao" },
                { 0x627E, L"z", L"zhao" }, { 0x6280, L"j", L"ji" }, { 0x628A, L"b", L"ba" },
                { 0x62A5, L"b", L"bao" }, { 0x62A8, L"a", L"an" }, { 0x6307, L"z", L"zhi" },
                { 0x636E, L"j", L"ju" }, { 0x6392, L"p", L"pai" }, { 0x63A5, L"j", L"jie" },
                { 0x63A8, L"t", L"tui" }, { 0x63D0, L"t", L"ti" }, { 0x641C, L"s", L"sou" },
                { 0x6509, L"z", L"ze" }, { 0x6539, L"g", L"gai" }, { 0x653E, L"f", L"fang" },
                { 0x653F, L"z", L"zheng" }, { 0x6570, L"s", L"shu" }, { 0x6587, L"w", L"wen" }, { 0x6590, L"f", L"fei" },
                { 0x65B0, L"x", L"xin" }, { 0x65AD, L"d", L"duan" }, { 0x65B9, L"f", L"fang" },
                { 0x65E5, L"r", L"ri" }, { 0x65F6, L"s", L"shi" }, { 0x660E, L"m", L"ming" },
                { 0x6613, L"y", L"yi" }, { 0x663F, L"x", L"xian" }, { 0x66F4, L"g", L"geng" },
                { 0x6700, L"z", L"zui" }, { 0x6708, L"y", L"yue" }, { 0x6709, L"y", L"you" },
                { 0x670D, L"f", L"fu" }, { 0x672C, L"b", L"ben" }, { 0x672F, L"s", L"shu" },
                { 0x673A, L"j", L"ji" }, { 0x6743, L"q", L"quan" }, { 0x6750, L"c", L"cai" },
                { 0x6761, L"t", L"tiao" }, { 0x6765, L"l", L"lai" }, { 0x677F, L"b", L"ban" },
                { 0x6790, L"x", L"xi" }, { 0x67E5, L"c", L"cha" }, { 0x6807, L"b", L"biao" },
                { 0x6837, L"y", L"yang" }, { 0x683C, L"g", L"ge" }, { 0x6846, L"k", L"kuang" },
                { 0x6848, L"a", L"an" }, { 0x68C0, L"j", L"jian" }, { 0x6A21, L"m", L"mo" },
                { 0x6B21, L"c", L"ci" }, { 0x6B63, L"z", L"zheng" }, { 0x6B64, L"c", L"ci" },
                { 0x6B65, L"b", L"bu" }, { 0x6B66, L"w", L"wu" }, { 0x6AFE, L"e", L"er" },
                { 0x6C42, L"q", L"qiu" }, { 0x6C49, L"h", L"han" }, { 0x6C5F, L"j", L"jiang" },
                { 0x6C7D, L"q", L"qi" }, { 0x6C90, L"m", L"mu" }, { 0x6CA1, L"m", L"mei" },
                { 0x6D41, L"l", L"liu" }, { 0x6D4B, L"c", L"ce" }, { 0x6D77, L"h", L"hai" },
                { 0x6D88, L"x", L"xiao" }, { 0x6DF1, L"s", L"shen" }, { 0x6E05, L"q", L"qing" }, { 0x6E90, L"y", L"yuan" },
                { 0x70B9, L"d", L"dian" }, { 0x70ED, L"r", L"re" }, { 0x7167, L"z", L"zhao" },
                { 0x7528, L"y", L"yong" }, { 0x7531, L"y", L"you" }, { 0x753B, L"h", L"hua" },
                { 0x754C, L"j", L"jie" }, { 0x757F, L"l", L"liu" }, { 0x767B, L"d", L"deng" },
                { 0x767D, L"b", L"bai" }, { 0x767E, L"b", L"bai" }, { 0x76EE, L"m", L"mu" },
                { 0x76F4, L"z", L"zhi" }, { 0x76F8, L"x", L"xiang" }, { 0x770B, L"k", L"kan" },
                { 0x771F, L"z", L"zhen" }, { 0x77EB, L"j", L"jiao" }, { 0x77F3, L"s", L"shi" },
                { 0x7801, L"m", L"ma" }, { 0x78A7, L"b", L"bi" }, { 0x793A, L"s", L"shi" },
                { 0x793E, L"s", L"she" }, { 0x795E, L"s", L"shen" }, { 0x79FB, L"y", L"yi" },
                { 0x7A0B, L"c", L"cheng" }, { 0x7A7A, L"k", L"kong" }, { 0x7ABF, L"g", L"gao" },
                { 0x7B97, L"s", L"suan" }, { 0x7BA1, L"g", L"guan" }, { 0x7CFB, L"x", L"xi" },
                { 0x7D22, L"s", L"suo" }, { 0x7D2B, L"z", L"zi" }, { 0x7EA7, L"j", L"ji" },
                { 0x7EBF, L"x", L"xian" }, { 0x7EC4, L"z", L"zu" }, { 0x7ED3, L"j", L"jie" },
                { 0x7EDF, L"t", L"tong" }, { 0x7EEA, L"x", L"xu" }, { 0x7F16, L"b", L"bian" },
                { 0x7F51, L"w", L"wang" }, { 0x7F6E, L"z", L"zhi" }, { 0x7F8E, L"m", L"mei" },
                { 0x8005, L"z", L"zhe" }, { 0x8054, L"l", L"lian" }, { 0x81EA, L"z", L"zi" },
                { 0x8272, L"s", L"se" }, { 0x82CE, L"a", L"an" }, { 0x83B7, L"h", L"huo" },
                { 0x8868, L"b", L"biao" }, { 0x88C5, L"z", L"zhuang" }, { 0x89C1, L"j", L"jian" },
                { 0x89C6, L"s", L"shi" }, { 0x89E3, L"j", L"jie" }, { 0x8A00, L"y", L"yan" },
                { 0x8BA1, L"j", L"ji" }, { 0x8BA4, L"r", L"ren" }, { 0x8BA9, L"r", L"rang" },
                { 0x8BB0, L"j", L"ji" }, { 0x8BBF, L"f", L"fang" }, { 0x8BBE, L"s", L"she" },
                { 0x8BFA, L"n", L"nuo" }, { 0x8BFB, L"d", L"du" }, { 0x8C03, L"d", L"diao" },
                { 0x8C46, L"d", L"dou" }, { 0x8C61, L"x", L"xiang" }, { 0x8C6A, L"h", L"hao" },
                { 0x8C9D, L"b", L"bei" }, { 0x8D44, L"z", L"zi" }, { 0x8DDF, L"g", L"gen" },
                { 0x8DEF, L"l", L"lu" }, { 0x8F6F, L"r", L"ruan" }, { 0x8F6C, L"z", L"zhuan" },
                { 0x8F93, L"s", L"shu" }, { 0x8FCE, L"y", L"ying" }, { 0x8FBF, L"f", L"fan" },
                { 0x9009, L"x", L"xuan" }, { 0x9010, L"z", L"zhu" }, { 0x901A, L"t", L"tong" },
                { 0x901F, L"s", L"su" }, { 0x900F, L"t", L"tou" }, { 0x901B, L"g", L"guang" },
                { 0x901D, L"s", L"shi" }, { 0x9020, L"z", L"zao" }, { 0x903B, L"l", L"luo" },
                { 0x9062, L"x", L"xian" }, { 0x9075, L"z", L"zun" }, { 0x91CD, L"z", L"zhong" },
                { 0x91CF, L"l", L"liang" }, { 0x91D1, L"j", L"jin" }, { 0x9488, L"z", L"zhen" },
                { 0x94A1, L"c", L"ci" }, { 0x94AE, L"a", L"an" }, { 0x94B1, L"q", L"qian" },
                { 0x952E, L"j", L"jian" }, { 0x957F, L"c", L"chang" }, { 0x95E8, L"m", L"men" },
                { 0x95ED, L"b", L"bi" }, { 0x95EE, L"w", L"wen" }, { 0x9601, L"g", L"ge" },
                { 0x961F, L"d", L"dui" }, { 0x9632, L"f", L"fang" }, { 0x9633, L"y", L"yang" },
                { 0x9634, L"y", L"yin" }, { 0x9636, L"j", L"jie" }, { 0x9644, L"f", L"fu" },
                { 0x966D, L"l", L"lu" }, { 0x966E, L"m", L"mo" }, { 0x96C6, L"j", L"ji" },
                { 0x96F6, L"l", L"ling" }, { 0x9752, L"q", L"qing" }, { 0x9759, L"j", L"jing" },
                { 0x9762, L"m", L"mian" }, { 0x97F3, L"y", L"yin" }, { 0x9875, L"y", L"ye" }, { 0x987F, L"d", L"dun" },
                { 0x98CE, L"f", L"feng" }, { 0x98DE, L"f", L"fei" }, { 0x9996, L"s", L"shou" },
                { 0x9999, L"x", L"xiang" }, { 0x9A6C, L"m", L"ma" }, { 0x9A71, L"q", L"qu" },
                { 0x9A8C, L"y", L"yan" }, { 0x9AD8, L"g", L"gao" }, { 0x9B45, L"m", L"mei" },
                { 0x9C81, L"l", L"lu" }, { 0x9EE4, L"m", L"mo" }, { 0x9F99, L"l", L"long" }
            };

            // Specialized exact Hanzi map for common desktop app names (微信, 支付宝, 计算器, 记事本, 浏览器, 终端, 设置, 工具, etc.)
            static const std::unordered_map<wchar_t, PinyinEntry> exactMap = {
                { L'微', { L'微', L"w", L"wei" } },
                { L'信', { L'信', L"x", L"xin" } },
                { L'计', { L'计', L"j", L"ji" } },
                { L'算', { L'算', L"s", L"suan" } },
                { L'器', { L'器', L"q", L"qi" } },
                { L'记', { L'记', L"j", L"ji" } },
                { L'事', { L'事', L"s", L"shi" } },
                { L'本', { L'本', L"b", L"ben" } },
                { L'流', { L'流', L"l", L"liu" } },
                { L'览', { L'览', L"l", L"lan" } },
                { L'设', { L'设', L"s", L"she" } },
                { L'置', { L'置', L"z", L"zhi" } },
                { L'文', { L'文', L"w", L"wen" } },
                { L'件', { L'件', L"j", L"jian" } },
                { L'夹', { L'夹', L"j", L"jia" } },
                { L'控', { L'控', L"k", L"kong" } },
                { L'制', { L'制', L"z", L"zhi" } },
                { L'板', { L'板', L"b", L"ban" } },
                { L'画', { L'画', L"h", L"hua" } },
                { L'图', { L'图', L"t", L"tu" } },
                { L'音', { L'音', L"y", L"yin" } },
                { L'乐', { L'乐', L"y", L"yue" } },
                { L'视', { L'视', L"s", L"shi" } },
                { L'频', { L'频', L"p", L"pin" } },
                { L'游', { L'游', L"y", L"you" } },
                { L'戏', { L'戏', L"x", L"xi" } },
                { L'工', { L'工', L"g", L"gong" } },
                { L'具', { L'具', L"j", L"ju" } },
                { L'资', { L'资', L"z", L"zi" } },
                { L'源', { L'源', L"y", L"yuan" } },
                { L'管', { L'管', L"g", L"guan" } },
                { L'理', { L'理', L"l", L"li" } },
                { L'队', { L'队', L"d", L"dui" } },
                { L'网', { L'网', L"w", L"wang" } },
                { L'络', { L'络', L"l", L"luo" } },
                { L'诊', { L'诊', L"z", L"zhen" } },
                { L'断', { L'断', L"d", L"duan" } },
                { L'进', { L'进', L"j", L"jin" } },
                { L'程', { L'程', L"c", L"cheng" } },
                { L'刺', { L'刺', L"c", L"ci" } },
                { L'激', { L'激', L"j", L"ji" } },
                { L'迅', { L'迅', L"x", L"xun" } },
                { L'雷', { L'雷', L"l", L"lei" } },
                { L'企', { L'企', L"q", L"qi" } },
                { L'业', { L'业', L"y", L"ye" } },
                { L'钉', { L'钉', L"d", L"ding" } },
                { L'飞', { L'飞', L"f", L"fei" } },
                { L'书', { L'书', L"s", L"shu" } },
                { L'百', { L'百', L"b", L"bai" } },
                { L'度', { L'度', L"d", L"du" } },
                { L'淘', { L'淘', L"t", L"tao" } },
                { L'宝', { L'宝', L"b", L"bao" } },
                { L'京', { L'京', L"j", L"jing" } },
                { L'东', { L'东', L"d", L"dong" } }
            };

            auto exactIt = exactMap.find(ch);
            if (exactIt != exactMap.end())
            {
                outInitial = exactIt->second.initial;
                outFull = exactIt->second.full;
                return true;
            }

            // Find in boundaries
            const Boundary* best = &boundaries[0];
            for (const auto& b : boundaries)
            {
                if (ch >= b.start)
                    best = &b;
                else
                    break;
            }
            outInitial = best->initial;
            outFull = best->full;
            return true;
        }

        return false;
    }
    static std::mutex s_cacheMutex;
    static std::unordered_map<std::wstring, std::pair<std::wstring, std::wstring>> s_cache;
    static constexpr size_t MAX_PINYIN_CACHE_SIZE = 2000;
}
std::wstring PinyinHelper::GetInitials(const std::wstring& text)
{
    if (text.empty()) return L"";

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(text);
        if (it != s_cache.end())
        {
            return it->second.first;
        }
    }

    std::wstring initials;
    initials.reserve(text.length());
    std::wstring curInit, curFull;
    for (wchar_t ch : text)
    {
        if (GetCharPinyin(ch, curInit, curFull))
        {
            initials += curInit;
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.size() >= MAX_PINYIN_CACHE_SIZE)
        {
            s_cache.clear();
        }
        s_cache[text].first = initials;
    }

    return initials;
}

std::wstring PinyinHelper::GetFullPinyin(const std::wstring& text)
{
    if (text.empty()) return L"";

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_cache.find(text);
        if (it != s_cache.end() && !it->second.second.empty())
        {
            return it->second.second;
        }
    }

    std::wstring full;
    full.reserve(text.length() * 4);
    std::wstring curInit, curFull;
    for (wchar_t ch : text)
    {
        if (GetCharPinyin(ch, curInit, curFull))
        {
            full += curFull;
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_cache.size() >= MAX_PINYIN_CACHE_SIZE)
        {
            s_cache.clear();
        }
        s_cache[text].second = full;
    }

    return full;
}

bool PinyinHelper::Match(const std::wstring& text, const std::wstring& queryLower, bool& isInitialsMatch, bool& isFullPinyinMatch)
{
    isInitialsMatch = false;
    isFullPinyinMatch = false;

    if (queryLower.empty() || text.empty())
        return false;

    std::wstring initials = GetInitials(text);
    if (!initials.empty() && initials.find(queryLower) != std::wstring::npos)
    {
        isInitialsMatch = true;
        return true;
    }

    std::wstring fullPinyin = GetFullPinyin(text);
    if (!fullPinyin.empty() && fullPinyin.find(queryLower) != std::wstring::npos)
    {
        isFullPinyinMatch = true;
        return true;
    }

    return false;
}
