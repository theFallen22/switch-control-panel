#pragma once
#include <stdbool.h>

// Ana döngü: ayar dosyasını izler, açıkken HTTP isteklerine cevap verir.
// *dur true olunca döner (Switch'te hiç olmaz; test için).
void sunucuDongusu(volatile bool* dur);
