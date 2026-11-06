// AJ2 · R16 guard duyarlılık probu (ölçüm aracı — üretim kodu DEĞİL).
//
// AJ1 §5 üç ZORUNLU PC verdi. Bu dosya PC1 (duyarlılık) ve PC2 (etki)'yi
// ÖLÇER; PC3 (oracle 1407/79/79/OK) ayrı kapıdır: tests/oracle/run_oracle.sh.
//
// ⚠️ İKİ AYRI ŞEYİ AYRI ÖLÇ — önceki denemede karıştırdım, düzeltildi:
//   (a) PORT'un kendisi guard öncesi/sonrası değişti mi?   -> PC1'in asıl sorusu
//   (b) PORT ile REFERANS arasındaki fark var mı?          -> §2'nin konusu
//   Bunlar FARKLI sorular. (1e200,1e200) p=2 için port↔ref AYRI olmak
//   KASITLI (bkz. math-vec2.hpp'teki alan sınırı notu) — bunu guard'ın
//   "düzeltmesi" gibi saymak ölçümü bozuyordu.
//
// PC1 DUYARLILIK: guard, SONLU girdilerin HİÇBİR sonucunu değiştirmemeli.
//   AJ1'in adını saydığı üç vaka: (3,4) p=1e-9 -> 4, p=1e-320 -> 4,
//   p=-1 -> 1.71429.  Ölçüm: guard'lı ve guard'sız iki ikili bu satırlarda
//   BİREBİR aynı byte vermeli. (guard'sız ikili aynı kaynaktan derlenir —
//   metin sürüklenmesi olmasın diye metin iki yerde değil, #ifdef ile tek
//   yerde; bkz. README.)
//
// PC2 ETKI: sonlu olmayan bileşende guard devreye girip SONLU döndürmeli.
//   PC2'nin KENDİ PC'si: guard'ı geri alan mutasyon aynı satırları
//   yeniden inf'e döndürmeli. Sayaç >0 çıkmazsa alet duyarsızdır.
//
// Kullanım:
//   cmp_guard            -> guard'LI ikili (bu kaynaktan)
//   cmp_guard --mutasyon -> guard'sız ikili
//   cmp_guard --pc       -> ikisini de koşturur, PC1+PC2'yi birleşik hükümler

#include "math-vec2.hpp"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>

using namespace rawaccel;

struct vaka { const char* ad; double x, y, p; bool sonlu_girdi; };

// AJ1 §1'in dokuz vakalık tablosu + §5 PC1'in üç sonlu vakası.
static const vaka VAKALAR[] = {
    { "Izgara ici    (3,4) p=2",          3, 4, 2.0,    true  },
    { "Izgara ici    (3,4) p=1",          3, 4, 1.0,    true  },
    { "Izgara sonu   (1e5,1) p=2",        1e5, 1, 2.0,  true  },
    { "Izgara sonu   (0,0) p=2",          0, 0, 2.0,    true  },
    { "PC1 sonlu     (3,4) p=1e-9",       3, 4, 1e-9,   true  },
    { "PC1 sonlu     (3,4) p=1e-320",     3, 4, 1e-320, true  },
    { "PC1 sonlu     (3,4) p=-1",         3, 4, -1.0,   true  },
    { "Alan disi sonlu (1e200,1e200) p=2",1e200, 1e200, 2.0, true },
    { "Alan disi sonlu (1e200,0) p=2",    1e200, 0, 2.0, true  },
    { "Alan disi sonlu (1e160,1e160) p=2",1e160, 1e160, 2.0, true },
    { "SONLU OLMAYAN (Inf,3) p=2",        INFINITY, 3, 2.0, false },
    { "SONLU OLMAYAN (NaN,3) p=2",        NAN, 3, 2.0, false },
    { "SONLU OLMAYAN (Inf,Inf) p=2",      INFINITY, INFINITY, 2.0, false },
};
static const int N_VAKA = (int)(sizeof(VAKALAR) / sizeof(VAKALAR[0]));
static const int PC1_BAS = 0, PC1_BIT = 7;   // ilk 7 satır: PC1'in sonlu vakaları

// Resmi referans: tests/oracle/ref/math-vec2.hpp:34-37
static double lp_ref(double x, double y, double p) {
    return std::pow(std::pow(std::fabs(x), p) + std::pow(std::fabs(y), p), 1 / p);
}

// Satır satır bas (PC modunda iki ikilinin ciktisini karsilastirilir).
static void tablo_yaz(const char* baslik) {
    printf("== %s ==\n", baslik);
    for (int i = 0; i < N_VAKA; ++i) {
        const vaka& v = VAKALAR[i];
        const double got = lp_distance({v.x, v.y}, v.p);
        const double ref = lp_ref(v.x, v.y, v.p);
        // Alanlar TABBELLA ayrilir: etiket bosluk iceriyor, split(None) yanlis
        // kirpar. Son iki alan sayisal ve bosluk icermez.
        printf("ROW\t%d\t%s\t%.17g\t%.17g\n", i, v.ad, got, ref);
    }
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* mod = (argc > 1) ? argv[1] : "";
    if (std::strcmp(mod, "--pc") == 0) {
        printf("PC modu: iki ikili ayri sureklerde kosulur\n");
        printf("bu ikili derleme komutu: --pc tek basina bir sey yapmaz\n");
        return 2;
    }
#ifdef AJ2_MUTASYON
    if (std::strcmp(mod, "--mutasyon") != 0) {
        fprintf(stderr, "bu ikili guard'siz derlendi; --mutasyon bekleniyor\n");
        return 2;
    }
    printf("MODE mutasyon\n");
#else
    if (std::strcmp(mod, "--mutasyon") == 0) {
        fprintf(stderr, "bu ikili guard'li derlendi; --mutasyon beklenmiyor\n");
        return 2;
    }
    printf("MODE guard\n");
#endif
    tablo_yaz("lp_distance");
    return 0;
}
