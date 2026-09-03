#include <stdio.h>
#include <string.h>
#include "font_cjk.h"

int main(void)
{
    /* Characters used in preview strings + tricky ones */
    const char *tests[] = {
        "宇多田ヒカル", "米津玄師", "前前前世", "夜に駆ける",
        "スパークル", "群青日和", "カタオモイ", "白日", "アニメ",
        "響", "薔薇", "邊", "髙", "﨑", "龍", "麺", "々", "〜", "…", "♪", "①"
    };
    unsigned int i;
    int missing = 0;

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        const char *p = tests[i];
        printf("%s =>", tests[i]);
        while (*p) {
            uint32_t cp = utf8_decode(&p);
            if (cp == 0 || cp == (uint32_t)-1) {
                printf(" [BAD]");
                missing++;
                break;
            }
            if (font_cjk_find(cp) < 0) {
                printf(" [MISS U+%04X]", cp);
                missing++;
            }
        }
        printf("\n");
    }
    printf("missing total: %d\n", missing);
    return missing ? 1 : 0;
}
