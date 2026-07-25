#include "PinyinHelper.h"
#include <cwctype>
#include <algorithm>
#include <unordered_map>
#include <list>
#include <vector>
#include <mutex>
#include <sstream>

namespace
{
    struct HanziEntry
    {
        wchar_t ch;
        const char* initial; // e.g. "w" or "z,c" for polyphonic
        const char* full;    // e.g. "wei" or "zhong,chong" for polyphonic
    };

    // Sorted array of CJK characters by wchar_t code point for std::lower_bound binary search.
    // Covers standard desktop software, OS tools, settings, system utilities, documents, and common vocabulary.
    static HanziEntry g_hanziTable[] = {

        { L'一', "y", "yi" }, { L'丁', "d", "ding" }, { L'七', "q", "qi" }, { L'万', "w", "wan" },
        { L'丈', "z", "zhang" }, { L'三', "s", "san" }, { L'上', "s", "shang" }, { L'下', "x", "xia" },
        { L'不', "b", "bu" }, { L'与', "y", "yu" }, { L'丑', "c", "chou" }, { L'专', "z", "zhuan" },
        { L'且', "q", "qie" }, { L'世', "s", "shi" }, { L'丘', "q", "qiu" }, { L'丙', "b", "bing" },
        { L'业', "y", "ye" }, { L'丛', "c", "cong" }, { L'东', "d", "dong" }, { L'丝', "s", "si" },
        { L'丢', "d", "diu" }, { L'两', "l", "liang" }, { L'严', "y", "yan" }, { L'丧', "s", "sang" },
        { L'个', "g", "ge" }, { L'中', "z", "zhong" }, { L'丰', "f", "feng" }, { L'串', "c", "chuan" },
        { L'临', "l", "lin" }, { L'丸', "w", "wan" }, { L'丹', "d", "dan" }, { L'为', "w", "wei" },
        { L'主', "z", "zhu" }, { L'丽', "l", "li" }, { L'举', "j", "ju" }, { L'乃', "n", "nai" },
        { L'久', "j", "jiu" }, { L'么', "m", "me" }, { L'义', "y", "yi" }, { L'之', "z", "zhi" },
        { L'乌', "w", "wu" }, { L'乎', "h", "hu" }, { L'乐', "y,l", "yue,le" }, { L'乒', "p", "ping" },
        { L'乓', "p", "pang" }, { L'乔', "q", "qiao" }, { L'乘', "c", "cheng" }, { L'九', "j", "jiu" },
        { L'乞', "q", "qi" }, { L'也', "y", "ye" }, { L'习', "x", "xi" }, { L'乡', "x", "xiang" },
        { L'书', "s", "shu" }, { L'买', "m", "mai" }, { L'乱', "l", "luan" }, { L'乳', "r", "ru" },
        { L'乾', "q", "qian" }, { L'了', "l", "le" }, { L'予', "y", "yu" }, { L'事', "s", "shi" },
        { L'二', "e", "er" }, { L'于', "y", "yu" }, { L'亏', "k", "kui" }, { L'云', "y", "yun" },
        { L'互', "h", "hu" }, { L'五', "w", "wu" }, { L'井', "j", "jing" }, { L'亚', "y", "ya" },
        { L'些', "x", "xie" }, { L'交', "j", "jiao" }, { L'亥', "h", "hai" }, { L'亦', "y", "yi" },
        { L'产', "c", "chan" }, { L'亨', "h", "heng" }, { L'亩', "m", "mu" }, { L'享', "x", "xiang" },
        { L'京', "j", "jing" }, { L'亭', "t", "ting" }, { L'亮', "l", "liang" }, { L'亲', "q", "qin" },
        { L'人', "r", "ren" }, { L'亿', "y", "yi" }, { L'什', "s", "shen" }, { L'仁', "r", "ren" },
        { L'仅', "j", "jin" }, { L'仆', "p", "pu" }, { L'仇', "c", "chou" }, { L'今', "j", "jin" },
        { L'介', "j", "jie" }, { L'仍', "r", "reng" }, { L'从', "c", "cong" }, { L'仑', "l", "lun" },
        { L'仓', "c", "cang" }, { L'仔', "z", "zi" }, { L'仕', "s", "shi" }, { L'他', "t", "ta" },
        { L'仗', "z", "zhang" }, { L'付', "f", "fu" }, { L'仙', "x", "xian" }, { L'代', "d", "dai" },
        { L'令', "l", "ling" }, { L'以', "y", "yi" }, { L'仪', "y", "yi" }, { L'们', "m", "men" },
        { L'仰', "y", "yang" }, { L'仲', "z", "zhong" }, { L'件', "j", "jian" }, { L'价', "j", "jia" },
        { L'任', "r", "ren" }, { L'份', "f", "fen" }, { L'仿', "f", "fang" }, { L'企', "q", "qi" },
        { L'伊', "y", "yi" }, { L'伍', "w", "wu" }, { L'伏', "f", "fu" }, { L'伐', "f", "fa" },
        { L'休', "x", "xiu" }, { L'众', "z", "zhong" }, { L'优', "y", "you" }, { L'伙', "h", "huo" },
        { L'会', "h", "hui" }, { L'伟', "w", "wei" }, { L'传', "c", "chuan" }, { L'伤', "s", "shang" },
        { L'伦', "l", "lun" }, { L'伪', "w", "wei" }, { L'伯', "b", "bo" }, { L'估', "g", "gu" },
        { L'伴', "b", "ban" }, { L'伶', "l", "ling" }, { L'伸', "s", "shen" }, { L'伺', "s", "si" },
        { L'似', "s", "si" }, { L'伽', "j", "jia" }, { L'但', "d", "dan" }, { L'位', "w", "wei" },
        { L'低', "d", "di" }, { L'住', "z", "zhu" }, { L'佐', "z", "zuo" }, { L'佑', "y", "you" }, { L'体', "t", "ti" },
        { L'何', "h", "he" }, { L'余', "y", "yu" }, { L'佛', "f", "fo" }, { L'作', "z", "zuo" },
        { L'你', "n", "ni" }, { L'佩', "p", "pei" }, { L'佳', "j", "jia" }, { L'使', "s", "shi" },
        { L'来', "l", "lai" }, { L'侈', "c", "chi" }, { L'例', "l", "li" }, { L'侍', "s", "shi" },
        { L'侏', "z", "zhu" }, { L'供', "g", "gong" }, { L'依', "y", "yi" }, { L'侠', "x", "xia" },
        { L'侦', "z", "zhen" }, { L'侧', "c", "ce" }, { L'侨', "q", "qiao" }, { L'快', "k", "kuai" },
        { L'侮', "w", "wu" }, { L'侯', "h", "hou" }, { L'侵', "q", "qin" }, { L'便', "b,p", "bian,pian" },
        { L'促', "c", "cu" }, { L'俄', "e", "e" }, { L'俊', "j", "jun" }, { L'俎', "z", "zu" },
        { L'俗', "s", "su" }, { L'俘', "f", "fu" }, { L'俚', "l", "li" }, { L'保', "b", "bao" },
        { L'俞', "y", "yu" }, { L'信', "x", "xin" }, { L'俩', "l", "lia" }, { L'俨', "y", "yan" },
        { L'修', "x", "xiu" }, { L'俱', "j", "ju" }, { L'俾', "b", "bi" }, { L'倍', "b", "bei" },
        { L'倒', "d", "dao" }, { L'倔', "j", "jue" }, { L'倘', "t", "tang" }, { L'候', "h", "hou" },
        { L'倚', "y", "yi" }, { L'借', "j", "jie" }, { L'倡', "c", "chang" }, { L'倦', "j", "juan" },
        { L'倪', "n", "ni" }, { L'债', "z", "zhai" }, { L'倾', "q", "qing" }, { L'假', "j", "jia" },
        { L'偃', "y", "yan" }, { L'停', "t", "ting" }, { L'偏', "p", "pian" }, { L'偕', "x", "xie" },
        { L'做', "z", "zuo" }, { L'停', "t", "ting" }, { L'健', "j", "jian" }, { L'傍', "b", "bang" },
        { L'储', "c", "chu" }, { L'催', "c", "cui" }, { L'傲', "a", "ao" }, { L'傻', "s", "sha" },
        { L'像', "x", "xiang" }, { L'僚', "l", "liao" }, { L'僧', "s", "seng" }, { L'儒', "r", "ru" },
        { L'儿', "e", "er" }, { L'允', "y", "yun" }, { L'元', "y", "yuan" }, { L'兄', "x", "xiong" },
        { L'充', "c", "chong" }, { L'兆', "z", "zhao" }, { L'先', "x", "xian" }, { L'光', "g", "guang" },
        { L'克', "k", "ke" }, { L'免', "m", "mian" }, { L'兑', "d", "dui" }, { L'兔', "t", "tu" },
        { L'党', "d", "dang" }, { L'兜', "d", "dou" }, { L'兢', "j", "jing" }, { L'入', "r", "ru" },
        { L'全', "q", "quan" }, { L'八', "b", "ba" }, { L'公', "g", "gong" }, { L'六', "l", "liu" },
        { L'兰', "l", "lan" }, { L'共', "g", "gong" }, { L'关', "g", "guan" }, { L'兴', "x", "xing" },
        { L'兵', "b", "bing" }, { L'其', "q", "qi" }, { L'具', "j", "ju" }, { L'典', "d", "dian" }, { L'兹', "z", "zi" },
        { L'养', "y", "yang" }, { L'兼', "j", "jian" }, { L'兽', "s", "shou" }, { L'冀', "j", "ji" },
        { L'内', "n", "nei" }, { L'冈', "g", "gang" }, { L'册', "c", "ce" }, { L'再', "z", "zai" },
        { L'冒', "m", "mao" }, { L'冗', "r", "rong" }, { L'写', "x", "xie" }, { L'军', "j", "jun" },
        { L'农', "n", "nong" }, { L'冠', "g", "guan" }, { L'冬', "d", "dong" }, { L'冰', "b", "bing" },
        { L'冲', "c", "chong" }, { L'决', "j", "jue" }, { L'况', "k", "kuang" }, { L'冷', "l", "leng" },
        { L'冻', "d", "dong" }, { L'净', "j", "jing" }, { L'凄', "q", "qi" }, { L'准', "z", "zhun" },
        { L'凉', "l", "liang" }, { L'凌', "l", "ling" }, { L'减', "j", "jian" }, { L'凑', "c", "cou" },
        { L'凛', "l", "lin" }, { L'凡', "f", "fan" }, { L'凤', "f", "feng" }, { L'凭', "p", "ping" },
        { L'凯', "k", "kai" }, { L'凰', "h", "huang" }, { L'出', "c", "chu" }, { L'击', "j", "ji" },
        { L'函', "h", "han" }, { L'凿', "z", "zao" }, { L'刀', "d", "dao" }, { L'刁', "d", "diao" },
        { L'分', "f", "fen" }, { L'切', "q", "qie" }, { L'刊', "k", "kan" }, { L'划', "h", "hua" },
        { L'列', "l", "lie" }, { L'刘', "l", "liu" }, { L'则', "z", "ze" }, { L'刚', "g", "gang" },
        { L'创', "c", "chuang" }, { L'初', "c", "chu" }, { L'删', "s", "shan" }, { L'判', "p", "pan" },
        { L'利', "l", "li" }, { L'别', "b", "bie" }, { L'刮', "g", "gua" }, { L'到', "d", "dao" },
        { L'制', "z", "zhi" }, { L'刷', "s", "shua" }, { L'券', "q", "quan" }, { L'刹', "s", "sha" },
        { L'刺', "c", "ci" }, { L'刻', "k", "ke" }, { L'刽', "g", "gui" }, { L'剁', "d", "duo" },
        { L'剂', "j", "ji" }, { L'剃', "t", "ti" }, { L'削', "x", "xiao" }, { L'前', "q", "qian" },
        { L'剑', "j", "jian" }, { L'剔', "t", "ti" }, { L'剖', "p", "pou" }, { L'剧', "j", "ju" },
        { L'剩', "s", "sheng" }, { L'剪', "j", "jian" }, { L'副', "f", "fu" }, { L'割', "g", "ge" },
        { L'力', "l", "li" }, { L'劝', "q", "quan" }, { L'办', "b", "ban" }, { L'功', "g", "gong" },
        { L'加', "j", "jia" }, { L'务', "w", "wu" }, { L'劣', "l", "lie" }, { L'动', "d", "dong" },
        { L'助', "z", "zhu" }, { L'努', "n", "nu" }, { L'劫', "j", "jie" }, { L'励', "l", "li" },
        { L'劲', "j", "jin" }, { L'劳', "l", "lao" }, { L'势', "s", "shi" }, { L'勇', "y", "yong" },
        { L'勉', "m", "mian" }, { L'勋', "x", "xun" }, { L'勒', "l", "le" }, { L'勾', "g", "gou" },
        { L'包', "b", "bao" }, { L'匆', "c", "cong" }, { L'匈', "x", "xiong" }, { L'葡', "p", "pu" },
        { L'化', "h", "hua" }, { L'北', "b", "bei" }, { L'匙', "c", "chi" }, { L'匹', "p", "pi" },
        { L'区', "q", "qu" }, { L'医', "y", "yi" }, { L'匾', "b", "bian" }, { L'十', "s", "shi" },
        { L'千', "q", "qian" }, { L'升', "s", "sheng" }, { L'午', "w", "wu" }, { L'半', "b", "ban" },
        { L'华', "h", "hua" }, { L'协', "x", "xie" }, { L'卑', "b", "bei" }, { L'卒', "z", "zu" },
        { L'卓', "z", "zhuo" }, { L'单', "d,s,c", "dan,shan,chan" }, { L'南', "n", "nan" }, { L'博', "b", "bo" },
        { L'卜', "b", "bu" }, { L'卞', "b", "bian" }, { L'卡', "k", "ka" }, { L'卢', "l", "lu" },
        { L'印', "y", "yin" }, { L'危', "w", "wei" }, { L'即', "j", "ji" }, { L'却', "q", "que" },
        { L'卵', "l", "luan" }, { L'卷', "j", "juan" }, { L'卸', "x", "xie" }, { L'卿', "q", "qing" },
        { L'厂', "c", "chang" }, { L'厄', "e", "e" }, { L'厅', "t", "ting" }, { L'历', "l", "li" },
        { L'厉', "l", "li" }, { L'压', "y", "ya" }, { L'厌', "y", "yan" }, { L'厕', "c", "ce" },
        { L'厘', "l", "li" }, { L'厚', "h", "hou" }, { L'原', "y", "yuan" }, { L'厢', "x", "xiang" },
        { L'去', "q", "qu" }, { L'县', "x", "xian" }, { L'参', "c", "can" }, { L'又', "y", "you" },
        { L'叉', "c", "cha" }, { L'及', "j", "ji" }, { L'友', "y", "you" }, { L'双', "s", "shuang" },
        { L'反', "f", "fan" }, { L'发', "f", "fa" }, { L'叔', "s", "shu" }, { L'取', "q", "qu" },
        { L'受', "s", "shou" }, { L'变', "b", "bian" }, { L'叙', "x", "xu" }, { L'叛', "p", "pan" },
        { L'叠', "d", "die" }, { L'口', "k", "kou" }, { L'古', "g", "gu" }, { L'句', "j", "ju" },
        { L'另', "l", "ling" }, { L'叨', "d", "dao" }, { L'叩', "k", "kou" }, { L'只', "z", "zhi" },
        { L'叫', "j", "jiao" }, { L'召', "z", "zhao" }, { L'叮', "d", "ding" }, { L'可', "k", "ke" },
        { L'台', "t", "tai" }, { L'史', "s", "shi" }, { L'右', "y", "you" }, { L'叶', "y", "ye" },
        { L'号', "h", "hao" }, { L'司', "s", "si" }, { L'叹', "t", "tan" }, { L'吃', "c", "chi" },
        { L'各', "g", "ge" }, { L'合', "h", "he" }, { L'吉', "j", "ji" }, { L'吊', "d", "diao" },
        { L'同', "t", "tong" }, { L'名', "m", "ming" }, { L'后', "h", "hou" }, { L'吐', "t", "tu" },
        { L'向', "x", "xiang" }, { L'吒', "z", "zha" }, { L'君', "j", "jun" }, { L'吝', "l", "lin" },
        { L'吟', "y", "yin" }, { L'吠', "f", "fei" }, { L'否', "f", "fou" }, { L'吧', "b", "ba" },
        { L'含', "h", "han" }, { L'听', "t", "ting" }, { L'启', "q", "qi" }, { L'吴', "w", "wu" },
        { L'吵', "c", "chao" }, { L'吸', "x", "xi" }, { L'吹', "c", "chui" }, { L'吻', "w", "wen" },
        { L'吾', "w", "wu" }, { L'呀', "y", "ya" }, { L'呆', "d", "dai" }, { L'呈', "c", "cheng" },
        { L'告', "g", "gao" }, { L'呐', "n", "na" }, { L'员', "y", "yuan" }, { L'呛', "q", "qiang" },
        { L'呜', "w", "wu" }, { L'呢', "n", "ne" }, { L'呦', "y", "you" }, { L'周', "z", "zhou" },
        { L'味', "w", "wei" }, { L'呵', "h", "he" }, { L'呸', "p", "pei" }, { L'呻', "s", "shen" },
        { L'呼', "h", "hu" }, { L'命', "m", "ming" }, { L'咀', "j", "ju" }, { L'咆', "p", "pao" },
        { L'和', "h", "he" }, { L'咖', "k", "ka" }, { L'咪', "m", "mi" }, { L'咬', "y", "yao" },
        { L'咳', "k", "ke" }, { L'咸', "x", "xian" }, { L'咽', "y", "yan" }, { L'品', "p", "pin" },
        { L'响', "x", "xiang" }, { L'哈', "h", "ha" }, { L'哥', "g", "ge" }, { L'哲', "z", "zhe" },
        { L'哺', "b", "bu" }, { L'哼', "h", "heng" }, { L'唆', "s", "suo" }, { L'唇', "c", "chun" },
        { L'唉', "a", "ai" }, { L'唤', "h", "huan" }, { L'唠', "l", "lao" }, { L'唱', "c", "chang" },
        { L'唾', "t", "tuo" }, { L'啄', "z", "zhuo" }, { L'商', "s", "shang" }, { L'啪', "p", "pa" },
        { L'啐', "c", "cui" }, { L'问', "w", "wen" }, { L'唯', "w", "wei" }, { L'唱', "c", "chang" },
        { L'念', "n", "nian" }, { L'啪', "p", "pa" }, { L'喂', "w", "wei" }, { L'善', "s", "shan" },
        { L'喇', "l", "la" }, { L'喉', "h", "hou" }, { L'喊', "h", "han" }, { L'喘', "c", "chuan" }, { L'喜', "x", "xi" },
        { L'喝', "h", "he" }, { L'喧', "x", "xuan" }, { L'喻', "y", "yu" }, { L'丧', "s", "sang" },
        { L'单', "d", "dan" }, { L'嗅', "x", "xiu" }, { L'喵', "m", "miao" }, { L'喷', "p", "pen" },
        { L'嗨', "h", "hai" }, { L'嘟', "d", "du" }, { L'嘴', "z", "zui" }, { L'器', "q", "qi" },
        { L'噩', "e", "e" }, { L'噪', "z", "zao" }, { L'噱', "j", "jue" }, { L'嚷', "r", "rang" },
        { L'嚼', "j", "jiao" }, { L'囊', "n", "nang" }, { L'四', "s", "si" }, { L'回', "h", "hui" },
        { L'因', "y", "yin" }, { L'团', "t", "tuan" }, { L'园', "y", "yuan" }, { L'困', "k", "kun" },
        { L'围', "w", "wei" }, { L'固', "g", "gu" }, { L'国', "g", "guo" }, { L'图', "t", "tu" },
        { L'圆', "y", "yuan" }, { L'圈', "q", "quan" }, { L'土', "t", "tu" }, { L'圣', "s", "sheng" },
        { L'在', "z", "zai" }, { L'地', "d", "di" }, { L'场', "c", "chang" }, { L'垃', "l", "la" },
        { L'坂', "b", "ban" }, { L'均', "j", "jun" }, { L'坊', "f", "fang" }, { L'坎', "k", "kan" },
        { L'坐', "z", "zuo" }, { L'坑', "k", "keng" }, { L'块', "k", "kuai" }, { L'坚', "j", "jian" },
        { L'坛', "t", "tan" }, { L'坝', "b", "ba" }, { L'坞', "w", "wu" }, { L'坟', "f", "fen" },
        { L'坡', "p", "po" }, { L'坤', "k", "kun" }, { L'坦', "t", "tan" }, { L'坪', "p", "ping" },
        { L'培', "p", "pei" }, { L'基', "j", "ji" }, { L'堂', "t", "tang" }, { L'堆', "d", "dui" },
        { L'堡', "b", "bao" }, { L'堪', "k", "kan" }, { L'塔', "t", "ta" }, { L'塑', "s", "su" },
        { L'境', "j", "jing" }, { L'墙', "q", "qiang" }, { L'增', "z", "zeng" }, { L'墨', "m", "mo" },
        { L'壁', "b", "bi" }, { L'士', "s", "shi" }, { L'壬', "r", "ren" }, { L'壮', "z", "zhuang" },
        { L'声', "s", "sheng" }, { L'壳', "k", "ke" }, { L'壶', "h", "hu" }, { L'壹', "y", "yi" },
        { L'处', "c", "chu" }, { L'备', "b", "bei" }, { L'复', "f", "fu" }, { L'夏', "x", "xia" },
        { L'外', "w", "wai" }, { L'多', "d", "duo" }, { L'夜', "y", "ye" }, { L'够', "g", "gou" },
        { L'大', "d", "da" }, { L'天', "t", "tian" }, { L'太', "t", "tai" }, { L'夫', "f", "fu" },
        { L'央', "y", "yang" }, { L'夯', "h", "hang" }, { L'失', "s", "shi" }, { L'头', "t", "tou" },
        { L'夷', "y", "yi" }, { L'夸', "k", "kua" }, { L'夹', "j", "jia" }, { L'夺', "d", "duo" },
        { L'奇', "q", "qi" }, { L'奈', "n", "nai" }, { L'奉', "f", "feng" }, { L'奋', "f", "fen" },
        { L'奏', "z", "zou" }, { L'契', "q", "qi" }, { L'奔', "b", "ben" }, { L'套', "t", "tao" },
        { L'奥', "a", "ao" }, { L'女', "n", "nv" }, { L'奴', "n", "nu" }, { L'奶', "n", "nai" },
        { L'奸', "j", "jian" }, { L'好', "h", "hao" }, { L'如', "r", "ru" }, { L'妇', "f", "fu" },
        { L'妈', "m", "ma" }, { L'妖', "y", "yao" }, { L'妙', "m", "miao" }, { L'妥', "t", "tuo" }, { L'妨', "f", "fang" },
        { L'妮', "n", "ni" }, { L'妹', "m", "mei" }, { L'妻', "q", "qi" }, { L'姆', "m", "mu" },
        { L'姊', "z", "zi" }, { L'始', "s", "shi" }, { L'姐', "j", "jie" }, { L'姑', "g", "gu" },
        { L'姓', "x", "xing" }, { L'委', "w", "wei" }, { L'姿', "z", "zi" }, { L'威', "w", "wei" },
        { L'娃', "w", "wa" }, { L'娄', "l", "lou" }, { L'娅', "y", "ya" }, { L'娆', "r", "rao" },
        { L'娇', "j", "jiao" }, { L'娜', "n", "na" }, { L'娟', "j", "juan" }, { L'娱', "y", "yu" },
        { L'娓', "w", "wei" }, { L'娘', "n", "niang" }, { L'娣', "d", "di" }, { L'娥', "e", "e" },
        { L'娩', "m", "mian" }, { L'娶', "q", "qu" }, { L'婆', "p", "po" }, { L'婉', "w", "wan" },
        { L'婚', "h", "hun" }, { L'婵', "c", "chan" }, { L'婷', "t", "ting" }, { L'媚', "m", "mei" },
        { L'媛', "y", "yuan" }, { L'媪', "a", "ao" }, { L'媳', "x", "xi" }, { L'媲', "p", "pi" },
        { L'嫂', "s", "sao" }, { L'嫉', "j", "ji" }, { L'嫌', "x", "xian" }, { L'嫡', "d", "di" },
        { L'嬉', "x", "xi" }, { L'婵', "c", "chan" }, { L'子', "z", "zi" }, { L'孔', "k", "kong" },
        { L'字', "z", "zi" }, { L'存', "c", "cun" }, { L'孙', "s", "sun" }, { L'孚', "f", "fu" },
        { L'孝', "x", "xiao" }, { L'孟', "m", "meng" }, { L'季', "j", "ji" }, { L'孤', "g", "gu" },
        { L'学', "x", "xue" }, { L'孩', "h", "hai" }, { L'孪', "l", "luan" }, { L'宁', "n", "ning" },
        { L'它', "t", "ta" }, { L'宅', "z", "zhai" }, { L'宇', "y", "yu" }, { L'守', "s", "shou" },
        { L'安', "a", "an" }, { L'宋', "s", "song" }, { L'完', "w", "wan" }, { L'宏', "h", "hong" },
        { L'宗', "z", "zong" }, { L'官', "g", "guan" }, { L'宙', "z", "zhou" }, { L'定', "d", "ding" },
        { L'宛', "w", "wan" }, { L'宜', "y", "yi" }, { L'宝', "b", "bao" }, { L'实', "s", "shi" },
        { L'宠', "c", "chong" }, { L'审', "s", "shen" }, { L'客', "k", "ke" }, { L'宣', "x", "xuan" },
        { L'室', "s", "shi" }, { L'宫', "g", "gong" }, { L'宰', "z", "zai" }, { L'害', "h", "hai" },
        { L'宴', "y", "yan" }, { L'宵', "x", "xiao" }, { L'家', "j", "jia" }, { L'容', "r", "rong" },
        { L'宽', "k", "kuan" }, { L'宾', "b", "bin" }, { L'宿', "s", "su" }, { L'寂', "j", "ji" },
        { L'寄', "j", "ji" }, { L'密', "m", "mi" }, { L'寇', "k", "kou" }, { L'富', "f", "fu" },
        { L'寐', "m", "mei" }, { L'寒', "h", "han" }, { L'寓', "y", "yu" }, { L'写', "x", "xie" },
        { L'寸', "c", "cun" }, { L'对', "d", "dui" }, { L'寻', "x", "xun" }, { L'导', "d", "dao" },
        { L'寿', "s", "shou" }, { L'封', "f", "feng" }, { L'射', "s", "she" }, { L'将', "j", "jiang" },
        { L'尉', "w", "wei" }, { L'尊', "z", "zun" }, { L'小', "x", "xiao" }, { L'少', "s", "shao" },
        { L'尔', "e", "er" }, { L'尖', "j", "jian" }, { L'尘', "c", "chen" }, { L'尚', "s", "shang" },
        { L'尝', "c", "chang" }, { L'尤', "y", "you" }, { L'就', "j", "jiu" }, { L'尺', "c", "chi" },
        { L'尼', "n", "ni" }, { L'尽', "j", "jin" }, { L'尾', "w", "wei" }, { L'局', "j", "ju" },
        { L'屁', "p", "pi" }, { L'居', "j", "ju" }, { L'屈', "q", "qu" }, { L'梯', "t", "ti" },
        { L'屋', "w", "wu" }, { L'屎', "s", "shi" }, { L'屏', "p", "ping" }, { L'展', "z", "zhan" },
        { L'屑', "x", "xie" }, { L'属', "s", "shu" }, { L'屠', "t", "tu" }, { L'屡', "l", "lv" },
        { L'山', "s", "shan" }, { L'屹', "y", "yi" }, { L'屿', "y", "yu" }, { L'岁', "s", "sui" },
        { L'岂', "q", "qi" }, { L'岗', "g", "gang" }, { L'岛', "d", "dao" }, { L'岩', "y", "yan" },
        { L'岳', "y", "yue" }, { L'岸', "a", "an" }, { L'峡', "x", "xia" }, { L'峰', "f", "feng" },
        { L'峻', "j", "jun" }, { L'崇', "c", "chong" }, { L'崎', "q", "qi" }, { L'崔', "c", "cui" },
        { L'崩', "b", "beng" }, { L'川', "c", "chuan" }, { L'州', "z", "zhou" }, { L'巡', "x", "xun" },
        { L'巢', "c", "chao" }, { L'工', "g", "gong" }, { L'左', "z", "zuo" }, { L'巧', "q", "qiao" },
        { L'巨', "j", "ju" }, { L'巩', "g", "gong" }, { L'巫', "w", "wu" }, { L'差', "c", "cha" },
        { L'己', "j", "ji" }, { L'已', "y", "yi" }, { L'巳', "s", "si" }, { L'巴', "b", "ba" },
        { L'巷', "x", "xiang" }, { L'巽', "x", "xun" }, { L'币', "b", "bi" }, { L'市', "s", "shi" },
        { L'布', "b", "bu" }, { L'帅', "s", "shuai" }, { L'师', "s", "shi" }, { L'希', "x", "xi" },
        { L'帐', "z", "zhang" }, { L'帕', "p", "pa" }, { L'帖', "t", "tie" }, { L'帝', "d", "di" },
        { L'带', "d", "dai" }, { L'席', "x", "xi" }, { L'帮', "b", "bang" }, { L'常', "c", "chang" },
        { L'帽', "m", "mao" }, { L'幅', "f", "fu" }, { L'干', "g", "gan" }, { L'平', "p", "ping" },
        { L'年', "n", "nian" }, { L'并', "b", "bing" }, { L'幸', "x", "xing" }, { L'幻', "h", "huan" },
        { L'幼', "y", "you" }, { L'幽', "y", "you" }, { L'广', "g", "guang" }, { L'庄', "z", "zhuang" },
        { L'庆', "q", "qing" }, { L'庇', "b", "bi" }, { L'床', "c", "chuang" }, { L'序', "x", "xu" },
        { L'卢', "l", "lu" }, { L'库', "k", "ku" }, { L'应', "y", "ying" }, { L'底', "d", "di" },
        { L'庖', "p", "pao" }, { L'店', "d", "dian" }, { L'庙', "m", "miao" }, { L'庚', "g", "geng" },
        { L'府', "f", "fu" }, { L'庞', "p", "pang" }, { L'废', "f", "fei" }, { L'度', "d", "du" },
        { L'座', "z", "zuo" }, { L'庭', "t", "ting" }, { L'康', "k", "kang" }, { L'庸', "y", "yong" },
        { L'廊', "l", "lang" }, { L'廖', "l", "liao" }, { L'延', "y", "yan" }, { L'廷', "t", "ting" },
        { L'建', "j", "jian" }, { L'开', "k", "kai" }, { L'异', "y", "yi" }, { L'弃', "q", "qi" },
        { L'弄', "n", "nong" }, { L'弊', "b", "bi" }, { L'弋', "y", "yi" }, { L'式', "s", "shi" },
        { L'弓', "g", "gong" }, { L'引', "y", "yin" }, { L'弗', "f", "fu" }, { L'弘', "h", "hong" },
        { L'弟', "d", "di" }, { L'张', "z,c", "zhang,chang" }, { L'弥', "m", "mi" }, { L'弦', "x", "xian" },
        { L'弯', "w", "wan" }, { L'弱', "r", "ruo" }, { L'弹', "d,t", "dan,tan" }, { L'强', "q", "qiang" },
        { L'归', "g", "gui" }, { L'当', "d", "dang" }, { L'录', "l", "lu" }, { L'彗', "h", "hui" },
        { L'彝', "y", "yi" }, { L'形', "x", "xing" }, { L'彦', "y", "yan" }, { L'彩', "c", "cai" },
        { L'彪', "b", "biao" }, { L'彬', "b", "bin" }, { L'彭', "p", "peng" }, { L'彰', "z", "zhang" },
        { L'影', "y", "ying" }, { L'役', "y", "yi" }, { L'彻', "c", "che" }, { L'彼', "b", "bi" },
        { L'往', "w", "wang" }, { L'征', "z", "zheng" }, { L'径', "j", "jing" }, { L'待', "d", "dai" },
        { L'旬', "x", "xun" }, { L'很', "h", "hen" }, { L'徘', "p", "pai" }, { L'徊', "h", "huai" },
        { L'律', "l", "lu" }, { L'徐', "x", "xu" }, { L'徒', "t", "tu" }, { L'得', "d", "de" },
        { L'循', "x", "xun" }, { L'微', "w", "wei" }, { L'德', "d", "de" }, { L'徽', "h", "hui" },
        { L'心', "x", "xin" }, { L'必', "b", "bi" }, { L'忆', "y", "yi" }, { L'忌', "j", "ji" },
        { L'忍', "r", "ren" }, { L'志', "z", "zhi" }, { L'忘', "w", "wang" }, { L'忙', "m", "mang" },
        { L'忠', "z", "zhong" }, { L'忧', "y", "you" }, { L'快', "k", "kuai" }, { L'念', "n", "nian" },
        { L'忱', "c", "chen" }, { L'忻', "x", "xin" }, { L'怀', "h", "huai" }, { L'态', "t", "tai" },
        { L'怂', "s", "song" }, { L'怜', "l", "lian" }, { L'思', "s", "si" }, { L'怠', "d", "dai" },
        { L'急', "j", "ji" }, { L'性', "x", "xing" }, { L'怨', "y", "yuan" }, { L'怪', "g", "guai" },
        { L'总', "z", "zong" }, { L'恋', "l", "lian" }, { L'恐', "k", "kong" }, { L'恕', "s", "shu" },
        { L'恢', "h", "hui" }, { L'息', "x", "xi" }, { L'恰', "q", "qia" }, { L'恳', "k", "ken" },
        { L'恶', "e", "e" }, { L'脑', "n", "nao" }, { L'恺', "k", "kai" }, { L'悉', "x", "xi" },
        { L'悍', "h", "han" }, { L'悔', "h", "hui" }, { L'悠', "y", "you" }, { L'患', "h", "huan" },
        { L'悦', "y", "yue" }, { L'您', "n", "nin" }, { L'悬', "x", "xuan" }, { L'悯', "m", "min" },
        { L'凄', "q", "qi" }, { L'情', "q", "qing" }, { L'惊', "j", "jing" }, { L'惜', "x", "xi" },
        { L'惠', "h", "hui" }, { L'惨', "c", "can" }, { L'惩', "c", "cheng" }, { L'惭', "c", "can" },
        { L'惭', "c", "can" }, { L'惮', "d", "dan" }, { L'惯', "g", "guan" }, { L'想', "x", "xiang" },
        { L'蠢', "c", "chun" }, { L'愁', "c", "chou" }, { L'愈', "y", "yu" }, { L'愉', "y", "yu" },
        { L'意', "y", "yi" }, { L'愚', "y", "yu" }, { L'感', "g", "gan" }, { L'愤', "f", "fen" },
        { L'愿', "y", "yuan" }, { L'慈', "c", "ci" }, { L'慌', "h", "huang" }, { L'慎', "s", "shen" }, { L'慕', "m", "mu" },
        { L'惨', "c", "can" }, { L'慢', "m", "man" }, { L'慧', "h", "hui" }, { L'概', "g", "gai" },
        { L'慰', "w", "wei" }, { L'懂', "d", "dong" }, { L'憩', "q", "qi" }, { L'激', "j", "ji" },
        { L'懒', "l", "lan" }, { L'戈', "g", "ge" }, { L'戊', "w", "wu" }, { L'戌', "x", "xu" },
        { L'戏', "x", "xi" }, { L'成', "c", "cheng" }, { L'我', "w", "wo" }, { L'戒', "j", "jie" },
        { L'或', "h", "huo" }, { L'战', "z", "zhan" }, { L'戚', "q", "qi" }, { L'截', "j", "jie" },
        { L'戴', "d", "dai" }, { L'户', "h", "hu" }, { L'房', "f", "fang" }, { L'所', "s", "suo" },
        { L'扁', "b", "bian" }, { L'扇', "s", "shan" }, { L'手', "s", "shou" }, { L'才', "c", "cai" },
        { L'扑', "p", "pu" }, { L'扒', "b", "ba" }, { L'打', "d", "da" }, { L'扔', "r", "reng" },
        { L'托', "t", "tuo" }, { L'扛', "k", "kang" }, { L'扣', "k", "kou" }, { L'扫', "s", "sao" },
        { L'执', "z", "zhi" }, { L'扩', "k", "kuo" }, { L'扫', "s", "sao" }, { L'扬', "y", "yang" },
        { L'扭', "n", "niu" }, { L'扮', "b", "ban" }, { L'扯', "c", "che" }, { L'扰', "r", "rao" },
        { L'扳', "b", "ban" }, { L'扶', "f", "fu" }, { L'批', "p", "pi" }, { L'扼', "e", "e" },
        { L'找', "z", "zhao" }, { L'技', "j", "ji" }, { L'抄', "c", "chao" }, { L'把', "b", "ba" },
        { L'抑', "y", "yi" }, { L'抓', "z", "zhua" }, { L'投', "t", "tou" }, { L'抗', "k", "kang" },
        { L'折', "z", "zhe" }, { L'抚', "f", "fu" }, { L'抢', "q", "qiang" }, { L'护', "h", "hu" },
        { L'报', "b", "bao" }, { L'拟', "n", "ni" }, { L'拔', "b", "ba" }, { L'拖', "t", "tuo" },
        { L'拘', "j", "ju" }, { L'招', "z", "zhao" }, { L'坡', "p", "po" }, { L'拟', "n", "ni" },
        { L'拥', "y", "yong" }, { L'拦', "l", "lan" }, { L'拨', "b", "bo" }, { L'择', "z", "ze" },
        { L'括', "k", "kuo" }, { L'拭', "s", "shi" }, { L'拼', "p", "pin" }, { L'拾', "s", "shi" },
        { L'拿', "n", "na" }, { L'持', "c", "chi" }, { L'挂', "g", "gua" }, { L'指', "z", "zhi" },
        { L'按', "a", "an" }, { L'挑', "t", "tiao" }, { L'挖', "w", "wa" }, { L'挚', "z", "zhi" },
        { L'挝', "w", "wo" }, { L'挨', "a", "ai" }, { L'挪', "n", "nuo" }, { L'挫', "c", "cuo" },
        { L'振', "z", "zhen" }, { L'挺', "t", "ting" }, { L'挽', "w", "wan" }, { L'捕', "b", "bu" },
        { L'捷', "j", "jie" }, { L'捺', "n", "na" }, { L'据', "j", "ju" }, { L'排', "p", "pai" },
        { L'掘', "j", "jue" }, { L'挂', "g", "gua" }, { L'控', "k", "kong" }, { L'推', "t", "tui" },
        { L'掩', "y", "yan" }, { L'措', "c", "cuo" }, { L'扫', "s", "sao" }, { L'描', "m", "miao" },
        { L'提', "t", "ti" }, { L'插', "c", "cha" }, { L'揖', "y", "yi" }, { L'握', "w", "wo" },
        { L'揣', "c", "chuai" }, { L'楷', "k", "kai" }, { L'揪', "j", "jiu" }, { L'揭', "j", "jie" },
        { L'搜', "s", "sou" }, { L'搞', "g", "gao" }, { L'搬', "b", "ban" }, { L'搭', "d", "da" },
        { L'撤', "c", "che" }, { L'播', "b", "bo" }, { L'撒', "s", "sa" }, { L'撕', "s", "si" },
        { L'撰', "z", "zhuan" }, { L'撞', "z", "zhuang" }, { L'撤', "c", "che" }, { L'撑', "c", "cheng" },
        { L'撬', "q", "qiao" }, { L'播', "b", "bo" }, { L'操', "c", "cao" }, { L'整', "z", "zheng" },
        { L'败', "b", "bai" }, { L'攻', "g", "gong" }, { L'放', "f", "fang" }, { L'政', "z", "zheng" },
        { L'故', "g", "gu" }, { L'效', "x", "xiao" }, { L'敌', "d", "di" }, { L'敏', "m", "min" },
        { L'救', "j", "jiu" }, { L'教', "j", "jiao" }, { L'敛', "l", "lian" }, { L'敢', "g", "gan" },
        { L'散', "s", "san" }, { L'敦', "d", "dun" }, { L'敬', "j", "jing" }, { L'数', "s", "shu" },
        { L'敲', "q", "qiao" }, { L'文', "w", "wen" }, { L'斋', "z", "zhai" }, { L'斑', "b", "ban" },
        { L'斗', "d", "dou" }, { L'料', "l", "liao" }, { L'斜', "x", "xie" }, { L'斟', "z", "zhen" },
        { L'斤', "j", "jin" }, { L'斥', "c", "chi" }, { L'斧', "f", "fu" }, { L'斩', "z", "zhan" },
        { L'断', "d", "duan" }, { L'斯', "s", "si" }, { L'新', "x", "xin" }, { L'方', "f", "fang" },
        { L'施', "s", "shi" }, { L'旁', "p", "pang" }, { L'旅', "l", "lv" }, { L'旋', "x", "xuan" },
        { L'族', "z", "zu" }, { L'旗', "q", "qi" }, { L'无', "w", "wu" }, { L'既', "j", "ji" },
        { L'日', "r", "ri" }, { L'旦', "d", "dan" }, { L'旧', "j", "jiu" }, { L'旨', "z", "zhi" },
        { L'早', "z", "zao" }, { L'旬', "x", "xun" }, { L'旭', "x", "xu" }, { L'时', "s", "shi" },
        { L'旺', "w", "wang" }, { L'昂', "a", "ang" }, { L'昆', "k", "kun" }, { L'昌', "c", "chang" },
        { L'明', "m", "ming" }, { L'昏', "h", "hun" }, { L'易', "y", "yi" }, { L'昔', "x", "xi" },
        { L'星', "s", "xing" }, { L'映', "y", "ying" }, { L'春', "c", "chun" }, { L'昧', "m", "mei" },
        { L'昨', "z", "zuo" }, { L'是', "s", "shi" }, { L'昼', "z", "zhou" }, { L'显', "x", "xian" },
        { L'晃', "h", "huang" }, { L'晋', "j", "jin" }, { L'晒', "s", "shai" }, { L'晓', "x", "xiao" },
        { L'晚', "w", "wan" }, { L'晟', "s", "sheng" }, { L'晨', "c", "chen" }, { L'普', "p", "pu" },
        { L'景', "j", "jing" }, { L'晴', "q", "qing" }, { L'晶', "j", "jing" }, { L'智', "z", "zhi" },
        { L'晾', "l", "liang" }, { L'暂', "z", "zan" }, { L'暮', "m", "mu" }, { L'暴', "b", "bao" },
        { L'日', "r", "ri" }, { L'曲', "q", "qu" }, { L'曳', "y", "ye" }, { L'更', "g", "geng" },
        { L'书', "s", "shu" }, { L'曹', "c", "cao" }, { L'曼', "m", "man" }, { L'曾', "z", "zeng" },
        { L'替', "t", "ti" }, { L'最', "z", "zui" }, { L'月', "y", "yue" }, { L'有', "y", "you" },
        { L'朋', "p", "peng" }, { L'服', "f", "fu" }, { L'朔', "s", "shuo" }, { L'朗', "l", "lang" },
        { L'望', "w", "wang" }, { L'朝', "c,z", "chao,zhao" }, { L'期', "q", "qi" }, { L'木', "m", "mu" },
        { L'未', "w", "wei" }, { L'末', "m", "mo" }, { L'本', "b", "ben" }, { L'札', "z", "zha" },
        { L'术', "s", "shu" }, { L'朱', "z", "zhu" }, { L'朴', "p", "pu" }, { L'朵', "d", "duo" },
        { L'机', "j", "ji" }, { L'杀', "s", "sha" }, { L'杂', "z", "za" }, { L'权', "q", "quan" },
        { L'杆', "g", "gan" }, { L'李', "l", "li" }, { L'杏', "x", "xing" }, { L'材', "c", "cai" },
        { L'村', "c", "cun" }, { L'杖', "z", "zhang" }, { L'杜', "d", "du" }, { L'束', "s", "shu" },
        { L'条', "t", "tiao" }, { L'来', "l", "lai" }, { L'杨', "y", "yang" }, { L'杭', "h", "hang" },
        { L'杯', "b", "bei" }, { L'杰', "j", "jie" }, { L'板', "b", "ban" }, { L'极', "j", "ji" },
        { L'构', "g", "gou" }, { L'枇', "p", "pi" }, { L'析', "x", "xi" }, { L'林', "l", "lin" },
        { L'果', "g", "guo" }, { L'枝', "z", "zhi" }, { L'枢', "s", "shu" }, { L'枣', "z", "zao" },
        { L'枪', "q", "qiang" }, { L'枫', "f", "feng" }, { L'枭', "x", "xiao" }, { L'枯', "k", "ku" },
        { L'柒', "q", "qi" }, { L'架', "j", "jia" }, { L'枷', "j", "jia" }, { L'柄', "b", "bing" },
        { L'某', "m", "mou" }, { L'染', "r", "ran" }, { L'柔', "r", "rou" }, { L'秩', "z", "zhi" },
        { L'柜', "g", "gui" }, { L'柠', "n", "ning" }, { L'查', "c", "cha" }, { L'柬', "j", "jian" },
        { L'柯', "k", "ke" }, { L'柱', "z", "zhu" }, { L'柳', "l", "liu" }, { L'柴', "c", "chai" },
        { L'栅', "z", "zha" }, { L'标', "b", "biao" }, { L'栈', "z", "zhan" }, { L'栗', "l", "li" },
        { L'校', "x", "xiao" }, { L'样', "y", "yang" }, { L'核', "h", "he" }, { L'根', "g", "gen" },
        { L'格', "g", "ge" }, { L'栽', "z", "zai" }, { L'桃', "t", "tao" }, { L'框', "k", "kuang" },
        { L'案', "a", "an" }, { L'桌', "z", "zhuo" }, { L'桐', "t", "tong" }, { L'桑', "s", "sang" },
        { L'桓', "h", "huan" }, { L'桔', "j", "ju" }, { L'桥', "q", "qiao" }, { L'梁', "l", "liang" },
        { L'梅', "m", "mei" }, { L'梦', "m", "meng" }, { L'梧', "w", "wu" }, { L'梨', "l", "li" },
        { L'梯', "t", "ti" }, { L'械', "x", "xie" }, { L'梳', "s", "shu" }, { L'梵', "f", "fan" }, { L'棋', "q", "qi" },
        { L'棍', "g", "gun" }, { L'棕', "z", "zong" }, { L'棚', "p", "peng" }, { L'棉', "m", "mian" },
        { L'棋', "q", "qi" }, { L'棱', "l", "leng" }, { L'棒', "b", "bang" }, { L'棋', "q", "qi" },
        { L'棚', "p", "peng" }, { L'栋', "d", "dong" }, { L'棠', "t", "tang" }, { L'植', "z", "zhi" },
        { L'森', "s", "sen" }, { L'栖', "q", "qi" }, { L'棵', "k", "ke" }, { L'椎', "z", "zhui" },
        { L'椒', "j", "jiao" }, { L'椭', "t", "tuo" }, { L'杨', "y", "yang" }, { L'椰', "y", "ye" },
        { L'概', "g", "gai" }, { L'榄', "l", "lan" }, { L'榆', "y", "yu" }, { L'樟', "z", "zhang" },
        { L'模', "m", "mo" }, { L'横', "h", "heng" }, { L'樱', "y", "ying" }, { L'橄', "g", "gan" },
        { L'橡', "x", "xiang" }, { L'橙', "c", "cheng" }, { L'橘', "j", "ju" }, { L'檀', "t", "tan" },
        { L'欠', "q", "qian" }, { L'次', "c", "ci" }, { L'欢', "h", "huan" }, { L'欣', "x", "xin" },
        { L'欧', "o", "ou" }, { L'欲', "y", "yu" }, { L'欺', "q", "qi" }, { L'款', "k", "kuan" },
        { L'歌', "g", "ge" }, { L'止', "z", "zhi" }, { L'正', "z", "zheng" }, { L'此', "c", "ci" },
        { L'步', "b", "bu" }, { L'武', "w", "wu" }, { L'歧', "q", "qi" }, { L'歪', "w", "wai" },
        { L'岁', "s", "sui" }, { L'归', "g", "gui" }, { L'死', "s", "si" }, { L'歼', "j", "jian" },
        { L'殁', "m", "mo" }, { L'殃', "y", "yang" }, { L'殉', "x", "xun" }, { L'殊', "s", "shu" },
        { L'残', "c", "can" }, { L'腐', "f", "fu" }, { L'殖', "z", "zhi" }, { L'段', "d", "duan" },
        { L'殷', "y", "yin" }, { L'殿', "d", "dian" }, { L'毁', "h", "hui" }, { L'毅', "y", "yi" },
        { L'毋', "w", "wu" }, { L'母', "m", "mu" }, { L'每', "m", "mei" }, { L'毒', "d", "du" },
        { L'比', "b", "bi" }, { L'毕', "b", "bi" }, { L'毗', "p", "pi" }, { L'毛', "m", "mao" },
        { L'毯', "t", "tan" }, { L'毫', "h", "hao" }, { L'氏', "s", "shi" }, { L'民', "m", "min" },
        { L'芒', "m", "mang" }, { L'气', "q", "qi" }, { L'氛', "f", "fen" }, { L'氧', "y", "yang" },
        { L'水', "s", "shui" }, { L'永', "y", "yong" }, { L'求', "q", "qiu" }, { L'汇', "h", "hui" },
        { L'汉', "h", "han" }, { L'汗', "h", "han" }, { L'汝', "r", "ru" }, { L'江', "j", "jiang" },
        { L'池', "c", "chi" }, { L'污', "w", "wu" }, { L'汪', "w", "wang" }, { L'汤', "t", "tang" },
        { L'汲', "j", "ji" }, { L'汽', "q", "qi" }, { L'沃', "w", "wo" }, { L'沈', "s", "shen" },
        { L'沉', "c", "chen" }, { L'沙', "s", "sha" }, { L'沛', "p", "pei" }, { L'沟', "g", "gou" },
        { L'没', "m", "mei" }, { L'沧', "c", "cang" }, { L'河', "h", "he" }, { L'油', "y", "you" },
        { L'治', "z", "zhi" }, { L'沼', "z", "zhao" }, { L'沿', "y", "yan" }, { L'泄', "x", "xie" },
        { L'泉', "q", "quan" }, { L'泊', "b", "bo" }, { L'法', "f", "fa" }, { L'泛', "f", "fan" },
        { L'泡', "p", "pao" }, { L'波', "b", "bo" }, { L'泣', "q", "qi" }, { L'泥', "n", "ni" },
        { L'注', "z", "zhu" }, { L'泪', "l", "lei" }, { L'泫', "x", "xuan" }, { L'泰', "t", "tai" },
        { L'泳', "y", "yong" }, { L'洋', "y", "yang" }, { L'洗', "x", "xi" }, { L'洛', "l", "luo" },
        { L'洞', "d", "dong" }, { L'津', "j", "jin" }, { L'泄', "x", "xie" }, { L'洪', "h", "hong" },
        { L'饵', "e", "er" }, { L'洲', "z", "zhou" }, { L'活', "h", "huo" }, { L'洽', "q", "qia" },
        { L'派', "p", "pai" }, { L'流', "l", "liu" }, { L'浅', "q", "qian" }, { L'测', "c", "ce" },
        { L'济', "j", "ji" }, { L'浑', "h", "hun" }, { L'浓', "n", "nong" }, { L'浙', "z", "zhe" },
        { L'浪', "l", "lang" }, { L'浮', "f", "fu" }, { L'浴', "y", "yu" }, { L'海', "h", "hai" },
        { L'涂', "t", "tu" }, { L'涉', "s", "she" }, { L'涌', "y", "yong" }, { L'涎', "x", "xian" },
        { L'凉', "l", "liang" }, { L'润', "r", "run" }, { L'涨', "z", "zhang" }, { L'涩', "s", "se" },
        { L'淑', "s", "shu" }, { L'淘', "t", "tao" }, { L'淡', "d", "dan" }, { L'深', "s", "shen" },
        { L'淳', "c", "chun" }, { L'混', "h", "hun" }, { L'淹', "y", "yan" }, { L'浅', "q", "qian" },
        { L'清', "q", "qing" }, { L'渡', "d", "du" }, { L'渣', "z", "zha" }, { L'渤', "b", "bo" },
        { L'温', "w", "wen" }, { L'港', "g", "gang" }, { L'渴', "k", "ke" }, { L'游', "y", "you" },
        { L'渺', "m", "miao" }, { L'湖', "h", "hu" }, { L'湘', "x", "xiang" }, { L'湛', "z", "zhan" },
        { L'湾', "w", "wan" }, { L'湿', "s", "shi" }, { L'溃', "k", "kui" }, { L'溅', "j", "jian" },
        { L'源', "y", "yuan" }, { L'溜', "l", "liu" }, { L'溢', "y", "yi" }, { L'溪', "x", "xi" },
        { L'溯', "s", "su" }, { L'溶', "r", "rong" }, { L'溺', "n", "ni" }, { L'滂', "p", "pang" },
        { L'沧', "c", "cang" }, { L'滋', "z", "zi" }, { L'滑', "h", "hua" }, { L'滞', "z", "zhi" },
        { L'滴', "d", "di" }, { L'满', "m", "man" }, { L'滤', "l", "lv" }, { L'滥', "l", "lan" },
        { L'滨', "b", "bin" }, { L'滩', "t", "tan" }, { L'漂', "p", "piao" }, { L'漆', "q", "qi" },
        { L'漏', "l", "lou" }, { L'演', "y", "yan" }, { L'漠', "m", "mo" }, { L'汉', "h", "han" },
        { L'涟', "l", "lian" }, { L'漩', "x", "xuan" }, { L'涨', "z", "zhang" }, { L'漫', "m", "man" },
        { L'漂', "p", "piao" }, { L'潮', "c", "chao" }, { L'澎', "p", "peng" }, { L'撤', "c", "che" },
        { L'澈', "c", "che" }, { L'澜', "l", "lan" }, { L'澳', "a", "ao" }, { L'激', "j", "ji" },
        { L'濒', "b", "bin" }, { L'火', "h", "huo" }, { L'灭', "m", "mie" }, { L'灯', "d", "deng" },
        { L'灰', "h", "hui" }, { L'灵', "l", "ling" }, { L'灶', "z", "zao" }, { L'灼', "z", "zhuo" },
        { L'灾', "z", "zai" }, { L'灿', "c", "can" }, { L'炉', "l", "lu" }, { L'炎', "y", "yan" },
        { L'炒', "c", "chao" }, { L'炕', "k", "kang" }, { L'炙', "z", "zhi" }, { L'炫', "x", "xuan" },
        { L'炸', "z", "zha" }, { L'硕', "s", "shuo" }, { L'烁', "s", "shuo" }, { L'烂', "l", "lan" },
        { L'烈', "l", "lie" }, { L'烘', "h", "hong" }, { L'烙', "l", "lao" }, { L'烛', "z", "zhu" },
        { L'烟', "y", "yan" }, { L'烤', "k", "kao" }, { L'烦', "f", "fan" }, { L'烧', "s", "shao" },
        { L'热', "r", "re" }, { L'焦', "j", "jiao" }, { L'煌', "h", "huang" }, { L'焰', "y", "yan" },
        { L'照', "z", "zhao" }, { L'熊', "x", "xiong" }, { L'熟', "s", "shu" }, { L'熨', "y", "yun" },
        { L'熬', "a", "ao" }, { L'熵', "s", "shang" }, { L'燃', "r", "ran" }, { L'爆', "b", "bao" },
        { L'爪', "z", "zhua" }, { L'爬', "p", "pa" }, { L'爱', "a", "ai" }, { L'爵', "j", "jue" },
        { L'父', "f", "fu" }, { L'爸', "b", "ba" }, { L'爷', "y", "ye" }, { L'爽', "s", "shuang" },
        { L'片', "p", "pian" }, { L'版', "b", "ban" }, { L'牌', "p", "pai" }, { L'牙', "y", "ya" },
        { L'牛', "n", "niu" }, { L'牟', "m", "mou" }, { L'牡', "m", "mu" }, { L'牢', "l", "lao" },
        { L'牧', "m", "mu" }, { L'物', "w", "wu" }, { L'牲', "s", "sheng" }, { L'牵', "q", "qian" },
        { L'特', "t", "te" }, { L'牺', "x", "xi" }, { L'狂', "k", "kuang" }, { L'狐', "h", "hu" },
        { L'狗', "g", "gou" }, { L'狠', "h", "hen" }, { L'独', "d", "du" }, { L'狮', "s", "shi" },
        { L'猫', "m", "miao" }, { L'猪', "z", "zhu" }, { L'献', "x", "xian" }, { L'猴', "h", "hou" },
        { L'玄', "x", "xuan" }, { L'率', "l,s", "lv,shuai" }, { L'玉', "y", "yu" }, { L'王', "w", "wang" },
        { L'玛', "m", "ma" }, { L'玩', "w", "wan" }, { L'玫', "m", "mei" }, { L'环', "h", "huan" },
        { L'现', "x", "xian" }, { L'玲', "l", "ling" }, { L'珊', "s", "shan" }, { L'珍', "z", "zhen" },
        { L'珠', "z", "zhu" }, { L'班', "b", "ban" }, { L'球', "q", "qiu" }, { L'理', "l", "li" },
        { L'琉', "l", "liu" }, { L'琴', "q", "qin" }, { L'琼', "q", "qiong" }, { L'瑞', "r", "rui" },
        { L'瑟', "s", "se" }, { L'瑰', "g", "gui" }, { L'瑶', "y", "yao" }, { L'璃', "l", "li" },
        { L'瓜', "g", "gua" }, { L'瓢', "p", "piao" }, { L'瓣', "b", "ban" }, { L'瓦', "w", "wa" },
        { L'瓶', "p", "ping" }, { L'瓷', "c", "ci" }, { L'甘', "g", "gan" }, { L'甚', "s", "shen" },
        { L'甜', "t", "tian" }, { L'生', "s", "sheng" }, { L'产', "c", "chan" }, { L'用', "y", "yong" },
        { L'甩', "s", "shuai" }, { L'甫', "f", "fu" }, { L'田', "t", "tian" }, { L'由', "y", "you" },
        { L'甲', "j", "jia" }, { L'申', "s", "shen" }, { L'男', "n", "nan" }, { L'甸', "d", "dian" },
        { L'界', "j", "jie" }, { L'畅', "c", "chang" }, { L'畏', "w", "wei" }, { L'畔', "p", "pan" },
        { L'留', "l", "liu" }, { L'畜', "x", "xu" }, { L'毕', "b", "bi" }, { L'略', "l", "lve" },
        { L'番', "f", "fan" }, { L'画', "h", "hua" }, { L'异', "y", "yi" }, { L'畴', "c", "chou" },
        { L'疏', "s", "shu" }, { L'疑', "y", "yi" }, { L'疗', "l", "liao" }, { L'疯', "f", "feng" },
        { L'疲', "p", "pi" }, { L'疾', "j", "ji" }, { L'痛', "t", "tong" }, { L'病', "b", "bing" },
        { L'痕', "h", "hen" }, { L'痛', "t", "tong" }, { L'痴', "c", "chi" }, { L'痒', "y", "yang" },
        { L'瘦', "s", "shou" }, { L'瘫', "t", "tan" }, { L'癌', "a", "ai" }, { L'癖', "p", "pi" },
        { L'登', "d", "deng" }, { L'发', "f", "fa" }, { L'白', "b", "bai" }, { L'百', "b", "bai" },
        { L'皂', "z", "zao" }, { L'貌', "m", "mao" }, { L'皆', "j", "jie" }, { L'皇', "h", "huang" },
        { L'皮', "p", "pi" }, { L'盈', "y", "ying" }, { L'益', "y", "yi" }, { L'昂', "a", "ang" },
        { L'盒', "h", "he" }, { L'盖', "g", "gai" }, { L'盘', "p", "pan" }, { L'盛', "s,c", "sheng,cheng" },
        { L'盟', "m", "meng" }, { L'目', "m", "mu" }, { L'直', "z", "zhi" }, { L'相', "x", "xiang" },
        { L'盼', "p", "pan" }, { L'盾', "d", "dun" }, { L'省', "s,x", "sheng,xing" }, { L'看', "k", "kan" },
        { L'真', "z", "zhen" }, { L'眠', "m", "mian" }, { L'眼', "y", "yan" }, { L'着', "z", "zhe" },
        { L'睁', "z", "zheng" }, { L'睛', "j", "jing" }, { L'睡', "s", "shui" }, { L'督', "d", "du" },
        { L'睦', "m", "mu" }, { L'瞄', "m", "miao" }, { L'瞅', "c", "chou" }, { L'瞎', "x", "xia" },
        { L'瞩', "z", "zhu" }, { L'矛', "m", "mao" }, { L'矢', "s", "shi" }, { L'知', "z", "zhi" },
        { L'短', "d", "duan" }, { L'矮', "a", "ai" }, { L'石', "s", "shi" }, { L'码', "m", "ma" },
        { L'砍', "k", "kan" }, { L'研', "y", "yan" }, { L'砖', "z", "zhuan" }, { L'砸', "z", "za" },
        { L'破', "p", "po" }, { L'砺', "l", "li" }, { L'砾', "l", "li" }, { L'础', "c", "chu" },
        { L'硕', "s", "shuo" }, { L'硬', "y", "ying" }, { L'确', "q", "que" }, { L'碗', "w", "wan" },
        { L'碟', "d", "die" }, { L'碧', "b", "bi" }, { L'碰', "p", "peng" }, { L'磁', "c", "ci" },
        { L'磨', "m", "mo" }, { L'示', "s", "shi" }, { L'社', "s", "she" }, { L'祁', "q", "qi" },
        { L'祀', "s", "si" }, { L'祝', "z", "zhu" }, { L'神', "s", "shen" }, { L'祥', "x", "xiang" },
        { L'票', "p", "piao" }, { L'祭', "j", "ji" }, { L'禅', "c", "chan" }, { L'福', "f", "fu" },
        { L'禹', "y", "yu" }, { L'离', "l", "li" }, { L'禾', "h", "he" }, { L'秀', "x", "xiu" },
        { L'私', "s", "si" }, { L'秃', "t", "tu" }, { L'秋', "q", "qiu" }, { L'种', "z,c", "zhong,chong" },
        { L'科', "k", "ke" }, { L'秒', "m", "miao" }, { L'秘', "m", "mi" }, { L'秦', "q", "qin" },
        { L'秤', "c", "cheng" }, { L'积', "j", "ji" }, { L'称', "c", "cheng" }, { L'程', "c", "cheng" },
        { L'稻', "d", "dao" }, { L'稿', "g", "gao" }, { L'穆', "m", "mu" }, { L'穴', "x", "xue" },
        { L'究', "j", "jiu" }, { L'穷', "q", "qiong" }, { L'空', "k", "kong" }, { L'穿', "c", "chuan" },
        { L'突', "t", "tu" }, { L'窃', "q", "qie" }, { L'窄', "z", "zhai" }, { L'窈', "y", "yao" },
        { L'窗', "c", "chuang" }, { L'立', "l", "li" }, { L'站', "z", "zhan" }, { L'竞', "j", "jing" },
        { L'章', "z", "zhang" }, { L'童', "t", "tong" }, { L'端', "d", "duan" }, { L'竹', "z", "zhu" },
        { L'竿', "g", "gan" }, { L'笃', "d", "du" }, { L'笑', "x", "xiao" }, { L'笔', "b", "bi" },
        { L'笙', "s", "sheng" }, { L'笛', "d", "di" }, { L'符', "f", "fu" }, { L'笨', "b", "ben" },
        { L'第', "d", "di" }, { L'等', "d", "deng" }, { L'筋', "j", "jin" }, { L'筏', "f", "fa" },
        { L'筑', "z", "zhu" }, { L'答', "d", "da" }, { L'策', "c", "ce" }, { L'算', "s", "suan" },
        { L'管', "g", "guan" }, { L'箭', "j", "jian" }, { L'箱', "x", "xiang" }, { L'箴', "z", "zhen" },
        { L'篇', "p", "pian" }, { L'篮', "l", "lan" }, { L'筹', "c", "chou" }, { L'籍', "j", "ji" },
        { L'米', "m", "mi" }, { L'粉', "f", "fen" }, { L'粒', "l", "li" }, { L'粗', "c", "cu" },
        { L'粘', "z,n", "zhan,nian" }, { L'肃', "s", "su" }, { L'粥', "z", "zhou" }, { L'粪', "f", "fen" },
        { L'粮', "l", "liang" }, { L'精', "j", "jing" }, { L'糕', "g", "gao" }, { L'糖', "t", "tang" },
        { L'糟', "z", "zao" }, { L'系', "x", "xi" }, { L'纠', "j", "jiu" }, { L'纪', "j", "ji" },
        { L'红', "h", "hong" }, { L'约', "y", "yue" }, { L'级', "j", "ji" }, { L'纳', "n", "na" },
        { L'纽', "n", "niu" }, { L'纯', "c", "chun" }, { L'纸', "z", "zhi" }, { L'级', "j", "ji" },
        { L'纷', "f", "fen" }, { L'纸', "z", "zhi" }, { L'纹', "w", "wen" }, { L'纺', "f", "fang" },
        { L'纽', "n", "niu" }, { L'线', "x", "xian" }, { L'练', "l", "lian" }, { L'组', "z", "zu" },
        { L'绅', "s", "shen" }, { L'细', "x", "xi" }, { L'织', "z", "zhi" }, { L'终', "z", "zhong" },
        { L'绊', "b", "ban" }, { L'绍', "s", "shao" }, { L'绎', "y", "yi" }, { L'经', "j", "jing" },
        { L'绑', "b", "bang" }, { L'绒', "r", "rong" }, { L'结', "j", "jie" }, { L'绕', "r", "rao" },
        { L'绘', "h", "hui" }, { L'给', "g", "gei" }, { L'绚', "x", "xuan" }, { L'统', "t", "tong" },
        { L'丝', "s", "si" }, { L'绝', "j", "jue" }, { L'统', "t", "tong" }, { L'绢', "j", "juan" },
        { L'绣', "x", "xiu" }, { L'绥', "s", "sui" }, { L'续', "x", "xu" }, { L'维', "w", "wei" },
        { L'绵', "m", "mian" }, { L'绷', "b", "beng" }, { L'绸', "c", "chou" }, { L'综', "z", "zong" },
        { L'绽', "z", "zhan" }, { L'绿', "l", "lv" }, { L'缀', "z", "zhui" }, { L'缄', "j", "jian" },
        { L'缅', "m", "mian" }, { L'缆', "l", "lan" }, { L'缉', "j", "ji" }, { L'锻', "d", "duan" },
        { L'缓', "h", "huan" }, { L'缔', "d", "di" }, { L'编', "b", "bian" }, { L'缘', "y", "yuan" },
        { L'缚', "f", "fu" }, { L'缜', "z", "zhen" }, { L'缝', "f", "feng" }, { L'缠', "c", "chan" },
        { L'缨', "y", "ying" }, { L'纤', "x", "xian" }, { L'缶', "f", "fou" }, { L'缸', "g", "gang" },
        { L'缺', "q", "que" }, { L'罐', "g", "guan" }, { L'网', "w", "wang" }, { L'罕', "h", "han" },
        { L'罗', "l", "luo" }, { L'罚', "f", "fa" }, { L'罢', "b", "ba" }, { L'罪', "z", "zui" },
        { L'置', "z", "zhi" }, { L'罚', "f", "fa" }, { L'署', "s", "shu" }, { L'罩', "z", "zhao" },
        { L'罪', "z", "zui" }, { L'蜀', "s", "shu" }, { L'羊', "y", "yang" }, { L'美', "m", "mei" },
        { L'羔', "g", "gao" }, { L'羚', "l", "ling" }, { L'羞', "x", "xiu" }, { L'翔', "x", "xiang" },
        { L'翘', "q", "qiao" }, { L'翠', "c", "cui" }, { L'翰', "h", "han" }, { L'翼', "y", "yi" },
        { L'耀', "y", "yao" }, { L'老', "l", "lao" }, { L'考', "k", "kao" }, { L'者', "z", "zhe" },
        { L'而', "e", "er" }, { L'耍', "s", "shua" }, { L'耐', "n", "nai" }, { L'端', "d", "duan" },
        { L'耒', "l", "lei" }, { L'耕', "g", "geng" }, { L'耳', "e", "er" }, { L'耶', "y", "ye" },
        { L'耸', "s", "song" }, { L'耻', "c", "chi" }, { L'聊', "l", "liao" }, { L'圣', "s", "sheng" },
        { L'聘', "p", "pin" }, { L'聚', "j", "ju" }, { L'闻', "w", "wen" }, { L'声', "s", "sheng" },
        { L'聪', "c", "cong" }, { L'联', "l", "lian" }, { L'肆', "s", "si" }, { L'肉', "r", "rou" },
        { L'肋', "l", "lei" }, { L'肌', "j", "ji" }, { L'肖', "x", "xiao" }, { L'肘', "z", "zhou" },
        { L'肚', "d", "du" }, { L'肠', "c", "chang" }, { L'股', "g", "gu" }, { L'肢', "z", "zhi" },
        { L'肤', "f", "fu" }, { L'肥', "f", "fei" }, { L'肩', "j", "jian" }, { L'育', "y", "yu" },
        { L'肺', "f", "fei" }, { L'肾', "s", "shen" }, { L'肿', "z", "zhong" }, { L'胀', "z", "zhang" },
        { L'胁', "x", "xie" }, { L'胃', "w", "wei" }, { L'胆', "d", "dan" }, { L'背', "b", "bei" },
        { L'胎', "t", "tai" }, { L'胖', "p", "pang" }, { L'胚', "p", "pei" }, { L'胜', "s", "sheng" },
        { L'胡', "h", "hu" }, { L'胞', "b", "bao" }, { L'胤', "y", "yin" }, { L'胥', "x", "xu" },
        { L'陇', "l", "long" }, { L'能', "n", "neng" }, { L'脂', "z", "zhi" }, { L'脉', "m", "mai" },
        { L'脊', "j", "ji" }, { L'脑', "n", "nao" }, { L'脱', "t", "tuo" }, { L'腐', "f", "fu" },
        { L'脯', "p", "pu" }, { L'腋', "y", "ye" }, { L'脸', "l", "lian" }, { L'脾', "p", "pi" },
        { L'舔', "t", "tian" }, { L'腆', "t", "tian" }, { L'腹', "f", "fu" }, { L'腺', "x", "xian" },
        { L'脑', "n", "nao" }, { L'腿', "t", "tui" }, { L'膀', "b", "bang" }, { L'臆', "y", "yi" },
        { L'臣', "c", "chen" }, { L'自', "z", "zi" }, { L'臭', "c", "chou" }, { L'至', "z", "zhi" },
        { L'致', "z", "zhi" }, { L'臻', "z", "zhen" }, { L'臼', "j", "jiu" }, { L'舅', "j", "jiu" },
        { L'舆', "y", "yu" }, { L'舌', "s", "she" }, { L'舍', "s", "she" }, { L'舒', "s", "shu" },
        { L'舐', "s", "shi" }, { L'舟', "z", "zhou" }, { L'航', "h", "hang" }, { L'般', "b", "ban" },
        { L'舰', "j", "jian" }, { L'舱', "c", "cang" }, { L'艘', "s", "sou" }, { L'艮', "g", "gen" },
        { L'良', "l", "liang" }, { L'艰', "j", "jian" }, { L'色', "s", "se" }, { L'艳', "y", "yan" },
        { L'艺', "y", "yi" }, { L'艾', "a", "ai" }, { L'节', "j", "jie" }, { L'芒', "m", "mang" },
        { L'芋', "y", "yu" }, { L'芝', "z", "zhi" }, { L'芥', "j", "jie" }, { L'芦', "l", "lu" },
        { L'芭', "b", "ba" }, { L'花', "h", "hua" }, { L'芳', "f", "fang" }, { L'芷', "z", "zhi" },
        { L'芸', "y", "yun" }, { L'芽', "y", "ya" }, { L'苇', "w", "wei" }, { L'苍', "c", "cang" },
        { L'苏', "s", "su" }, { L'苗', "m", "miao" }, { L'苛', "k", "ke" }, { L'苞', "b", "bao" },
        { L'苟', "g", "gou" }, { L'若', "r", "ruo" }, { L'苦', "k", "ku" }, { L'英', "y", "ying" },
        { L'苹', "p", "ping" }, { L'范', "f", "fan" }, { L'茄', "q", "qie" }, { L'茅', "m", "mao" },
        { L'茉', "m", "mo" }, { L'茜', "q", "qian" }, { L'茧', "j", "jian" }, { L'茫', "m", "mang" },
        { L'茬', "c", "cha" }, { L'茱', "z", "zhu" }, { L'兹', "z", "zi" }, { L'茶', "c", "cha" },
        { L'草', "c", "cao" }, { L'荒', "h", "huang" }, { L'荔', "l", "li" }, { L'荚', "j", "jia" },
        { L'荣', "r", "rong" }, { L'药', "y", "yao" }, { L'荷', "h", "he" }, { L'荻', "d", "di" },
        { L'莉', "l", "li" }, { L'莎', "s", "sha" }, { L'莫', "m", "mo" }, { L'莱', "l", "lai" },
        { L'莲', "l", "lian" }, { L'获', "h", "huo" }, { L'莹', "y", "ying" }, { L'莺', "y", "ying" },
        { L'莽', "m", "mang" }, { L'菊', "j", "ju" }, { L'菌', "j", "jun" }, { L'菜', "c", "cai" },
        { L'菠', "b", "bo" }, { L'菩', "p", "pu" }, { L'华', "h", "hua" }, { L'萎', "w", "wei" },
        { L'萍', "p", "ping" }, { L'营', "y", "ying" }, { L'黄', "h", "huang" }, { L'萧', "x", "xiao" },
        { L'萨', "s", "sa" }, { L'落', "l", "luo" }, { L'葆', "b", "bao" }, { L'著', "z", "zhu" },
        { L'葛', "g", "ge" }, { L'董', "d", "dong" }, { L'葡', "p", "pu" }, { L'葵', "k", "kui" },
        { L'蒂', "d", "di" }, { L'蒋', "j", "jiang" }, { L'蒙', "m", "meng" }, { L'蒜', "s", "suan" },
        { L'蒲', "p", "pu" }, { L'蒸', "z", "zheng" }, { L'蓝', "l", "lan" }, { L'蓟', "j", "ji" },
        { L'蓬', "p", "peng" }, { L'蔑', "m", "mie" }, { L'蔓', "m", "man" }, { L'蔚', "w", "wei" },
        { L'蔡', "c", "cai" }, { L'藏', "c,z", "cang,zang" }, { L'藤', "t", "teng" }, { L'虎', "h", "hu" },
        { L'虑', "l", "lv" }, { L'虚', "x", "xu" }, { L'虞', "y", "yu" }, { L'虫', "c", "chong" },
        { L'虹', "h", "hong" }, { L'蚁', "y", "yi" }, { L'蛇', "s", "she" }, { L'蛋', "d", "dan" },
        { L'蛤', "g", "ge" }, { L'蛛', "z", "zhu" }, { L'蚌', "b", "bang" }, { L'蚕', "c", "can" },
        { L'蛮', "m", "man" }, { L'蜂', "f", "feng" }, { L'蜗', "w", "wo" }, { L'蛛', "z", "zhu" },
        { L'蜻', "q", "qing" }, { L'蝉', "c", "chan" }, { L'蝎', "x", "xie" }, { L'融', "r", "rong" },
        { L'螺', "l", "luo" }, { L'蟋', "x", "xi" }, { L'血', "x", "xue" }, { L'行', "x,h", "xing,hang" },
        { L'衔', "x", "xian" }, { L'街', "j", "jie" }, { L'衙', "y", "ya" }, { L'卫', "w", "wei" },
        { L'冲', "c", "chong" }, { L'衣', "y", "yi" }, { L'表', "b", "biao" }, { L'衬', "c", "chen" },
        { L'衫', "s", "shan" }, { L'衰', "s", "shuai" }, { L'衷', "z", "zhong" }, { L'袁', "y", "yuan" },
        { L'袅', "n", "niao" }, { L'袈', "j", "jia" }, { L'袋', "d", "dai" }, { L'袍', "p", "pao" },
        { L'袒', "t", "tan" }, { L'袖', "x", "xiu" }, { L'袜', "w", "wa" }, { L'被', "b", "bei" },
        { L'袭', "x", "xi" }, { L'裁', "c", "cai" }, { L'裂', "l", "lie" }, { L'装', "z", "zhuang" },
        { L'裕', "y", "yu" }, { L'裙', "q", "qun" }, { L'裤', "k", "ku" }, { L'裳', "s", "shang" },
        { L'裴', "p", "pei" }, { L'裸', "l", "luo" }, { L'裹', "g", "guo" }, { L'褪', "t", "tui" },
        { L'襄', "x", "xiang" }, { L'西', "x", "xi" }, { L'要', "y", "yao" }, { L'覆', "f", "fu" },
        { L'见', "j", "jian" }, { L'观', "g", "guan" }, { L'规', "g", "gui" }, { L'觅', "m", "mi" },
        { L'视', "s", "shi" }, { L'览', "l", "lan" }, { L'觉', "j", "jue" }, { L'角', "j", "jiao" },
        { L'解', "j,x", "jie,xie" }, { L'触', "c", "chu" }, { L'言', "y", "yan" }, { L'订', "d", "ding" },
        { L'计', "j", "ji" }, { L'讯', "x", "xun" }, { L'讨', "t", "tao" }, { L'让', "r", "rang" },
        { L'讪', "s", "shan" }, { L'讫', "q", "qi" }, { L'训', "x", "xun" }, { L'议', "y", "yi" },
        { L'讯', "x", "xun" }, { L'记', "j", "ji" }, { L'讲', "j", "jiang" }, { L'讳', "h", "hui" },
        { L'讴', "o", "ou" }, { L'讶', "y", "ya" }, { L'钠', "n", "na" }, { L'许', "x", "xu" },
        { L'论', "l", "lun" }, { L'讼', "s", "song" }, { L'讽', "f", "feng" }, { L'设', "s", "she" },
        { L'访', "f", "fang" }, { L'诀', "j", "jue" }, { L'证', "z", "zheng" }, { L'评', "p", "ping" },
        { L'沮', "j", "ju" }, { L'识', "s", "shi" }, { L'诈', "z", "zha" }, { L'诉', "s", "su" },
        { L'诊', "z", "zhen" }, { L'词', "c", "ci" }, { L'译', "y", "yi" }, { L'试', "s", "shi" },
        { L'诗', "s", "shi" }, { L'诚', "c", "cheng" }, { L'话', "h", "hua" }, { L'诞', "d", "dan" },
        { L'诡', "g", "gui" }, { L'询', "x", "xuan" }, { L'谐', "x", "xie" }, { L'话', "h", "hua" },
        { L'该', "g", "gai" }, { L'祥', "x", "xiang" }, { L'语', "y", "yu" }, { L'误', "w", "wu" },
        { L'说', "s", "shuo" }, { L'请', "q", "qing" }, { L'诸', "z", "zhu" }, { L'诺', "n", "nuo" },
        { L'读', "d", "du" }, { L'课', "k", "ke" }, { L'谁', "s", "shei" }, { L'调', "d,t", "diao,tiao" },
        { L'谈', "t", "tan" }, { L'谊', "y", "yi" }, { L'谋', "m", "mou" }, { L'谍', "d", "die" },
        { L'谎', "h", "huang" }, { L'谐', "x", "xie" }, { L'谓', "w", "wei" }, { L'谚', "y", "yan" },
        { L'谜', "m", "mi" }, { L'谢', "x", "xie" }, { L'谣', "y", "yao" }, { L'谤', "b", "bang" },
        { L'谦', "q", "qian" }, { L'谨', "j", "jin" }, { L'谩', "m", "man" }, { L'谬', "m", "miu" },
        { L'谱', "p", "pu" }, { L'谷', "g", "gu" }, { L'豆', "d", "dou" }, { L'岂', "q", "qi" },
        { L'象', "x", "xiang" }, { L'豪', "h", "hao" }, { L'貌', "m", "mao" }, { L'贝', "b", "bei" },
        { L'贞', "z", "zhen" }, { L'负', "f", "fu" }, { L'贡', "g", "gong" }, { L'财', "c", "cai" },
        { L'责', "z", "ze" }, { L'贤', "x", "xian" }, { L'败', "b", "bai" }, { L'账', "z", "zhang" },
        { L'货', "h", "huo" }, { L'质', "z", "zhi" }, { L'贩', "f", "fan" }, { L'贪', "t", "tan" },
        { L'贫', "p", "pin" }, { L'贬', "b", "bian" }, { L'购', "g", "gou" }, { L'贮', "z", "zhu" },
        { L'贯', "g", "guan" }, { L'贰', "e", "er" }, { L'贱', "j", "jian" }, { L'贴', "t", "tie" },
        { L'贵', "g", "gui" }, { L'贷', "d", "dai" }, { L'贸', "m", "mao" }, { L'费', "f", "fei" },
        { L'贺', "h", "he" }, { L'贻', "y", "yi" }, { L'贼', "z", "zei" }, { L'贾', "j", "jia" },
        { L'贿', "h", "hui" }, { L'赁', "l", "lin" }, { L'赂', "l", "lu" }, { L'赃', "z", "zang" },
        { L'资', "z", "zi" }, { L'赊', "s", "she" }, { L'赋', "f", "fu" }, { L'赌', "d", "du" },
        { L'赎', "s", "shu" }, { L'赏', "s", "shang" }, { L'赐', "c", "ci" }, { L'赔', "p", "pei" },
        { L'赖', "l", "lai" }, { L'赚', "z", "zhuan" }, { L'赛', "s", "sai" }, { L'赞', "z", "zan" },
        { L'赠', "z", "zeng" }, { L'赡', "s", "shan" }, { L'赢', "y", "ying" }, { L'赤', "c", "chi" },
        { L'赫', "h", "he" }, { L'走', "z", "zou" }, { L'赴', "f", "fu" }, { L'赵', "z", "zhao" },
        { L'起', "q", "qi" }, { L'趁', "c", "chen" }, { L'超', "c", "chao" }, { L'越', "y", "yue" }, { L'趋', "q", "qu" },
        { L'足', "z", "zu" }, { L'跃', "y", "yue" }, { L'跑', "p", "pao" }, { L'距', "j", "ju" },
        { L'跟', "g", "gen" }, { L'路', "l", "lu" }, { L'跳', "t", "tiao" }, { L'踏', "t", "ta" },
        { L'踢', "t", "ti" }, { L'踩', "c", "cai" }, { L'踪', "z", "zong" }, { L'身', "s", "shen" },
        { L'躬', "g", "gong" }, { L'躯', "q", "qu" }, { L'车', "c", "che" }, { L'轨', "g", "gui" },
        { L'轩', "x", "xuan" }, { L'转', "z", "zhuan" }, { L'轮', "l", "lun" }, { L'软', "r", "ruan" },
        { L'轰', "h", "hong" }, { L'轻', "q", "qing" }, { L'载', "z,z", "zai,zai" }, { L'轿', "j", "jiao" },
        { L'较', "j", "jiao" }, { L'辄', "z", "zhe" }, { L'辅', "f", "fu" }, { L'辆', "l", "liang" },
        { L'辈', "b", "bei" }, { L'辉', "h", "hui" }, { L'辍', "c", "chuo" }, { L'辈', "b", "bei" },
        { L'输', "s", "shu" }, { L'辖', "x", "xia" }, { L'辕', "y", "yuan" }, { L'辛', "x", "xin" },
        { L'辟', "p", "pi" }, { L'辣', "l", "la" }, { L'办', "b", "ban" }, { L'辩', "b", "bian" },
        { L'农', "n", "nong" }, { L'迅', "x", "xun" }, { L'迎', "y", "ying" }, { L'近', "j", "jin" },
        { L'返', "f", "fan" }, { L'还', "h", "hai" }, { L'这', "z", "zhe" }, { L'进', "j", "jin" },
        { L'远', "y", "yuan" }, { L'违', "w", "wei" }, { L'连', "l", "lian" }, { L'迟', "c", "chi" },
        { L'迫', "p", "po" }, { L'述', "s", "shu" }, { L'迹', "j", "ji" }, { L'追', "z", "zhui" },
        { L'退', "t", "tui" }, { L'送', "s", "song" }, { L'逃', "t", "tao" }, { L'逆', "n", "ni" },
        { L'选', "x", "xuan" }, { L'逊', "x", "xun" }, { L'透', "t", "tou" }, { L'逐', "z", "zhu" },
        { L'递', "d", "di" }, { L'途', "t", "tu" }, { L'通', "t", "tong" }, { L'逛', "g", "guang" },
        { L'逝', "s", "shi" }, { L'呈', "c", "cheng" }, { L'速', "s", "su" }, { L'造', "z", "zao" },
        { L'逢', "f", "feng" }, { L'抓', "z", "zhua" }, { L'逸', "y", "yi" }, { L'逻', "l", "luo" },
        { L'遍', "b", "bian" }, { L'遏', "e", "e" }, { L'道', "d", "dao" }, { L'遗', "y", "yi" },
        { L'遭', "z", "zao" }, { L'遮', "z", "zhe" }, { L'遵', "z", "zun" }, { L'避', "b", "bi" },
        { L'邦', "b", "bang" }, { L'邪', "x", "xie" }, { L'邮', "y", "you" }, { L'邻', "l", "lin" },
        { L'郁', "y", "yu" }, { L'郊', "j", "jiao" }, { L'郎', "l", "lang" }, { L'郑', "z", "zheng" },
        { L'郡', "j", "jun" }, { L'部', "b", "bu" }, { L'郭', "g", "guo" }, { L'都', "d", "dou" },
        { L'酉', "y", "you" }, { L'配', "p", "pei" }, { L'酒', "j", "jiu" }, { L'酷', "k", "ku" },
        { L'酸', "s", "suan" }, { L'醉', "z", "zui" }, { L'采', "c", "cai" }, { L'释', "s", "shi" },
        { L'里', "l", "li" }, { L'重', "z,c", "zhong,chong" }, { L'野', "y", "ye" }, { L'量', "l", "liang" },
        { L'金', "j", "jin" }, { L'针', "z", "zhen" }, { L'钓', "d", "diao" }, { L'钟', "z", "zhong" },
        { L'钢', "g", "gang" }, { L'铁', "t", "tie" }, { L'卸', "x", "xie" }, { L'钱', "q", "qian" },
        { L'钻', "z", "zuan" }, { L'铁', "t", "tie" }, { L'铃', "l", "ling" }, { L'铅', "q", "qian" },
        { L'铆', "m", "mao" }, { L'铜', "t", "tong" }, { L'铝', "l", "lv" }, { L'银', "y", "yin" },
        { L'铸', "z", "zhu" }, { L'铺', "p", "pu" }, { L'链', "l", "lian" }, { L'销', "x", "xiao" },
        { L'锁', "s", "suo" }, { L'锅', "g", "guo" }, { L'锤', "c", "chui" }, { L'锋', "f", "feng" },
        { L'错', "c", "cuo" }, { L'锚', "m", "mao" }, { L'锡', "x", "xi" }, { L'锦', "j", "jin" },
        { L'键', "j", "jian" }, { L'镀', "d", "du" }, { L'镇', "z", "zhen" }, { L'镜', "j", "jing" },
        { L'镣', "l", "liao" }, { L'镭', "l", "lei" }, { L'长', "c,z", "chang,zhang" }, { L'门', "m", "men" },
        { L'闪', "s", "shan" }, { L'闭', "b", "bi" }, { L'开', "k", "kai" }, { L'润', "r", "run" },
        { L'闲', "x", "xian" }, { L'间', "j", "jian" }, { L'闵', "m", "min" }, { L'闷', "m", "men" },
        { L'闸', "z", "zha" }, { L'闹', "n", "nao" }, { L'闺', "g", "gui" }, { L'闻', "w", "wen" },
        { L'闽', "m", "min" }, { L'阀', "f", "fa" }, { L'阁', "g", "ge" }, { L'阅', "y", "yue" },
        { L'院', "y", "yuan" }, { L'阔', "k", "kuan" }, { L'队', "d", "dui" }, { L'阳', "y", "yang" },
        { L'阴', "y", "yin" }, { L'阵', "z", "zhen" }, { L'阶', "j", "jie" }, { L'阻', "z", "zu" },
        { L'阿', "a", "a" }, { L'陀', "t", "tuo" }, { L'附', "f", "fu" }, { L'际', "j", "ji" },
        { L'陆', "l", "lu" }, { L'陈', "c", "chen" }, { L'降', "j,x", "jiang,xiang" }, { L'限', "x", "xian" },
        { L'陕', "s", "shan" }, { L'院', "y", "yuan" }, { L'除', "c", "chu" }, { L'陪', "p", "pei" },
        { L'阴', "y", "yin" }, { L'陵', "l", "ling" }, { L'陷', "x", "xian" }, { L'陆', "l", "lu" },
        { L'隆', "l", "long" }, { L'隐', "y", "yin" }, { L'隔', "g", "ge" }, { L'障', "z", "zhang" },
        { L'雄', "x", "xiong" }, { L'雅', "y", "ya" }, { L'集', "j", "ji" }, { L'雇', "g", "gu" },
        { L'雌', "c", "ci" }, { L'雏', "c", "chu" }, { L'雕', "d", "diao" }, { L'雨', "y", "yu" },
        { L'雪', "x", "xue" }, { L'雯', "w", "wen" }, { L'零', "l", "ling" }, { L'雷', "l", "lei" },
        { L'雹', "b", "bao" }, { L'雾', "w", "wu" }, { L'需', "x", "xu" }, { L'震', "z", "zhen" },
        { L'霉', "m", "mei" }, { L'霍', "h", "huo" }, { L'霓', "n", "ni" }, { L'霖', "l", "lin" },
        { L'霜', "s", "shuang" }, { L'霞', "x", "xia" }, { L'霸', "b", "ba" }, { L'露', "l", "lu" },
        { L'霹', "p", "pi" }, { L'青', "q", "qing" }, { L'靖', "j", "jing" }, { L'静', "j", "jing" },
        { L'非', "f", "fei" }, { L'靠', "k", "kao" }, { L'面', "m", "mian" }, { L'革', "g", "ge" },
        { L'靴', "x", "xue" }, { L'鞭', "b", "bian" }, { L'音', "y", "yin" }, { L'韵', "y", "yun" },
        { L'韶', "s", "shao" }, { L'页', "y", "ye" }, { L'顶', "d", "ding" }, { L'顷', "q", "qing" },
        { L'项', "x", "xiang" }, { L'顺', "s", "shun" }, { L'须', "x", "xu" }, { L'顽', "w", "wan" },
        { L'顾', "g", "gu" }, { L'顿', "d", "dun" }, { L'颁', "b", "ban" }, { L'颂', "s", "song" },
        { L'预', "y", "yu" }, { L'领', "l", "ling" }, { L'颇', "p", "po" }, { L'频', "p", "pin" },
        { L'颖', "y", "ying" }, { L'颗', "k", "ke" }, { L'题', "t", "ti" }, { L'额', "e", "e" },
        { L'风', "f", "feng" }, { L'飘', "p", "piao" }, { L'飞', "f", "fei" }, { L'食', "s", "shi" },
        { L'餐', "c", "can" }, { L'饥', "j", "ji" }, { L'饭', "f", "fan" }, { L'饮', "y", "yin" },
        { L'饱', "b", "bao" }, { L'饰', "s", "shi" }, { L'饼', "b", "bing" }, { L'饿', "e", "e" },
        { L'馆', "g", "guan" }, { L'首', "s", "shou" }, { L'香', "x", "xiang" }, { L'马', "m", "ma" }, { L'驭', "y", "yu" },
        { L'驯', "x", "xun" }, { L'驰', "c", "chi" }, { L'驱', "q", "qu" }, { L'驳', "b", "bo" },
        { L'驴', "l", "lv" }, { L'驶', "s", "shi" }, { L'驹', "j", "ju" }, { L'驻', "z", "zhu" },
        { L'驼', "t", "tuo" }, { L'驾', "j", "jia" }, { L'骂', "m", "ma" }, { L'骑', "q", "qi" },
        { L'骗', "p", "pian" }, { L'骚', "s", "sao" }, { L'骨', "g", "gu" }, { L'高', "g", "gao" },
        { L'鬼', "g", "gui" }, { L'魂', "h", "hun" }, { L'魅', "m", "mei" }, { L'魔', "m", "mo" },
        { L'鱼', "y", "yu" }, { L'鲁', "l", "lu" }, { L'鲜', "x", "xian" }, { L'鸟', "n", "niao" },
        { L'鸠', "j", "jiu" }, { L'鸣', "m", "ming" }, { L'鸥', "o", "ou" }, { L'鸦', "y", "ya" },
        { L'鸭', "y", "ya" }, { L'鸯', "y", "yang" }, { L'鸽', "g", "ge" }, { L'鸿', "h", "hong" },
        { L'鹅', "e", "e" }, { L'鹤', "h", "he" }, { L'鹰', "y", "ying" }, { L'鹿', "l", "lu" },
        { L'麒', "q", "qi" }, { L'麦', "m", "mai" }, { L'麻', "m", "ma" }, { L'黄', "h", "huang" },
        { L'黎', "l", "li" }, { L'黑', "h", "hei" }, { L'默', "m", "mo" }, { L'鼎', "d", "ding" },
        { L'鼓', "g", "gu" }, { L'鼠', "s", "shu" }, { L'鼻', "b", "bi" }, { L'齐', "q", "qi" },
        { L'齿', "c", "chi" }, { L'龙', "l", "long" }, { L'龟', "g", "gui" }
    };

    static const size_t g_hanziTableCount = sizeof(g_hanziTable) / sizeof(g_hanziTable[0]);

    struct PinyinVariant
    {
        std::vector<std::wstring> initials;
        std::vector<std::wstring> fulls;
    };

    // Fast split helper for comma-separated polyphonic entries
    std::vector<std::wstring> SplitCsv(const char* csv)
    {
        std::vector<std::wstring> result;
        if (!csv) return result;
        std::string s(csv);
        std::stringstream ss(s);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            if (!item.empty())
            {
                std::wstring witem(item.begin(), item.end());
                result.push_back(witem);
            }
        }
        return result;
    }

    bool LookupHanzi(wchar_t ch, PinyinVariant& variant)
    {
        static std::once_flag s_sortOnce;
        std::call_once(s_sortOnce, []() {
            // Sort g_hanziTable by wchar_t code point once at runtime
            HanziEntry* tbl = const_cast<HanziEntry*>(g_hanziTable);
            std::sort(tbl, tbl + g_hanziTableCount, [](const HanziEntry& a, const HanziEntry& b) {
                return a.ch < b.ch;
            });
        });

        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') || (ch >= L'0' && ch <= L'9'))
        {
            wchar_t lower = static_cast<wchar_t>(towlower(ch));
            std::wstring str(1, lower);
            variant.initials.push_back(str);
            variant.fulls.push_back(str);
            return true;
        }

        // Binary search in g_hanziTable
        auto it = std::lower_bound(g_hanziTable, g_hanziTable + g_hanziTableCount, ch,
            [](const HanziEntry& entry, wchar_t val) {
                return entry.ch < val;
            });

        if (it != g_hanziTable + g_hanziTableCount && it->ch == ch)
        {
            variant.initials = SplitCsv(it->initial);
            variant.fulls = SplitCsv(it->full);
            return true;
        }

        return false;
    }

    struct PinyinCacheValue
    {
        std::wstring primaryInitials;
        std::wstring primaryFull;
        std::vector<std::wstring> allInitialVariants;
        std::vector<std::wstring> allFullVariants;
    };

    class ThreadSafePinyinLruCache
    {
    public:
        static constexpr size_t kMaxCapacity = 1000;

        bool Get(const std::wstring& key, PinyinCacheValue& outValue)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_map.find(key);
            if (it == m_map.end())
                return false;

            m_list.splice(m_list.begin(), m_list, it->second);
            outValue = it->second->second;
            return true;
        }

        void Put(const std::wstring& key, const PinyinCacheValue& value)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_map.find(key);
            if (it != m_map.end())
            {
                it->second->second = value;
                m_list.splice(m_list.begin(), m_list, it->second);
                return;
            }

            if (m_list.size() >= kMaxCapacity)
            {
                auto last = m_list.back();
                m_map.erase(last.first);
                m_list.pop_back();
            }

            m_list.push_front({ key, value });
            m_map[key] = m_list.begin();
        }

    private:
        std::mutex m_mutex;
        std::list<std::pair<std::wstring, PinyinCacheValue>> m_list;
        std::unordered_map<std::wstring, decltype(m_list)::iterator> m_map;
    };

    static ThreadSafePinyinLruCache g_pinyinCache;

    PinyinCacheValue ComputePinyinVariants(const std::wstring& text)
    {
        PinyinCacheValue cached;
        if (text.empty())
            return cached;

        std::vector<std::wstring> currentInitialsList = { L"" };
        std::vector<std::wstring> currentFullsList = { L"" };

        std::wstring primaryInitials;
        std::wstring primaryFull;

        for (wchar_t ch : text)
        {
            PinyinVariant variant;
            if (LookupHanzi(ch, variant))
            {
                primaryInitials += variant.initials.front();
                primaryFull += variant.fulls.front();

                std::vector<std::wstring> nextInitials;
                for (const auto& existing : currentInitialsList)
                {
                    for (const auto& init : variant.initials)
                    {
                        nextInitials.push_back(existing + init);
                    }
                }
                if (nextInitials.size() > 16) nextInitials.resize(16); // Bound combinatorial expansion
                currentInitialsList = std::move(nextInitials);

                std::vector<std::wstring> nextFulls;
                for (const auto& existing : currentFullsList)
                {
                    for (const auto& full : variant.fulls)
                    {
                        nextFulls.push_back(existing + full);
                    }
                }
                if (nextFulls.size() > 16) nextFulls.resize(16); // Bound combinatorial expansion
                currentFullsList = std::move(nextFulls);
            }
            else
            {
                wchar_t lower = static_cast<wchar_t>(towlower(ch));
                primaryInitials += lower;
                primaryFull += lower;
                for (auto& s : currentInitialsList) s += lower;
                for (auto& s : currentFullsList) s += lower;
            }
        }

        cached.primaryInitials = primaryInitials;
        cached.primaryFull = primaryFull;
        cached.allInitialVariants = std::move(currentInitialsList);
        cached.allFullVariants = std::move(currentFullsList);
        return cached;
    }
}

std::wstring PinyinHelper::GetInitials(const std::wstring& text)
{
    if (text.empty()) return L"";

    PinyinCacheValue val;
    if (g_pinyinCache.Get(text, val))
    {
        return val.primaryInitials;
    }

    val = ComputePinyinVariants(text);
    g_pinyinCache.Put(text, val);
    return val.primaryInitials;
}

std::wstring PinyinHelper::GetFullPinyin(const std::wstring& text)
{
    if (text.empty()) return L"";

    PinyinCacheValue val;
    if (g_pinyinCache.Get(text, val))
    {
        return val.primaryFull;
    }

    val = ComputePinyinVariants(text);
    g_pinyinCache.Put(text, val);
    return val.primaryFull;
}

bool PinyinHelper::Match(const std::wstring& text, const std::wstring& queryLower, bool& isInitialsMatch, bool& isFullPinyinMatch)
{
    isInitialsMatch = false;
    isFullPinyinMatch = false;

    if (queryLower.empty() || text.empty())
        return false;

    PinyinCacheValue val;
    if (!g_pinyinCache.Get(text, val))
    {
        val = ComputePinyinVariants(text);
        g_pinyinCache.Put(text, val);
    }

    for (const auto& init : val.allInitialVariants)
    {
        if (!init.empty() && init.find(queryLower) != std::wstring::npos)
        {
            isInitialsMatch = true;
            return true;
        }
    }

    for (const auto& full : val.allFullVariants)
    {
        if (!full.empty() && full.find(queryLower) != std::wstring::npos)
        {
            isFullPinyinMatch = true;
            return true;
        }
    }

    return false;
}
