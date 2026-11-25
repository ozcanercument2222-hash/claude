// UTF-8 source (MSVC: /utf-8).
#include "Lang.h"

#include <atomic>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

namespace vstm::i18n
{
namespace
{
struct Row
{
    const char* key;
    const char* text[kNumLangs];     // en, tr, de, fr, it, ru, zh
};

// clang-format off
const Row rows[] =
{
    //------------------------------------------------------------- card labels
    { "KEY",              { "KEY", "TON", "TONART", "TONALITÉ", "TONALITÀ", "ТОНАЛЬНОСТЬ", "调性" } },
    { "SCALE",            { "SCALE", "DİZİ", "SKALA", "GAMME", "SCALA", "ЛАД", "音阶" } },
    { "PITCH CORRECTION", { "PITCH CORRECTION", "PERDE DÜZELTME", "TONHÖHENKORREKTUR", "CORRECTION", "CORREZIONE", "КОРРЕКЦИЯ ВЫСОТЫ", "音高校正" } },
    { "RETUNE SPEED",     { "RETUNE SPEED", "DÜZELTME HIZI", "KORREKTURTEMPO", "VITESSE DE CORRECTION", "VELOCITÀ DI CORREZIONE", "СКОРОСТЬ КОРРЕКЦИИ", "校正速度" } },
    { "HUMANIZE",         { "HUMANIZE", "DOĞALLIK", "HUMANISIEREN", "HUMANISER", "UMANIZZA", "ЕСТЕСТВЕННОСТЬ", "人性化" } },
    { "ROBOTISM",         { "ROBOTISM", "ROBOTISM", "ROBOTISM", "ROBOTISM", "ROBOTISM", "ROBOTISM", "ROBOTISM" } },
    { "Robotic",          { "Robotic", "Robotik", "Robotisch", "Robotique", "Robotico", "Робот", "机械" } },
    { "(More artificial)",{ "(More artificial)", "(Daha Yapay)", "(Künstlicher)", "(Plus artificiel)", "(Più artificiale)", "(Искусственнее)", "（更人工）" } },
    { "Human",            { "Human", "İnsansı", "Menschlich", "Humain", "Umano", "Живой", "自然" } },
    { "(More natural)",   { "(More natural)", "(Daha Doğal)", "(Natürlicher)", "(Plus naturel)", "(Più naturale)", "(Естественнее)", "（更自然）" } },
    { "Balanced",         { "Balanced", "Dengeli", "Ausgewogen", "Équilibré", "Bilanciato", "Баланс", "平衡" } },
    { "PARAMETERS",       { "PARAMETERS", "PARAMETRELER", "PARAMETER", "PARAMÈTRES", "PARAMETRI", "ПАРАМЕТРЫ", "参数" } },
    { "knob.vibrato",     { "SCALE", "VİBRATO", "VIBRATO", "VIBRATO", "VIBRATO", "ВИБРАТО", "颤音" } },
    { "FORMANT",          { "FORMANT", "FORMANT", "FORMANT", "FORMANT", "FORMANTE", "ФОРМАНТА", "共振峰" } },
    { "PITCH RANGE",      { "PITCH RANGE", "TRANSPOZE", "TRANSPONIEREN", "TRANSPOSITION", "TRASPOSIZIONE", "ТРАНСПОЗИЦИЯ", "移调" } },
    { "MIX",              { "MIX", "MİKS", "MIX", "MIX", "MIX", "МИКС", "混合" } },
    { "AUTOTUNE ON",      { "AUTOTUNE ON", "AUTOTUNE AÇIK", "AUTOTUNE AN", "AUTOTUNE ACTIVÉ", "AUTOTUNE ATTIVO", "АВТОТЮН ВКЛ", "自动修音 开" } },
    { "AUTOTUNE OFF",     { "AUTOTUNE OFF", "AUTOTUNE KAPALI", "AUTOTUNE AUS", "AUTOTUNE DÉSACTIVÉ", "AUTOTUNE DISATTIVO", "АВТОТЮН ВЫКЛ", "自动修音 关" } },
    { "REALTIME",         { "REALTIME", "GERÇEK ZAMAN", "ECHTZEIT", "TEMPS RÉEL", "TEMPO REALE", "РЕАЛЬНОЕ ВРЕМЯ", "实时" } },

    //------------------------------------------------------------- tooltips
    { "tip.key",
      { "Key: the tonic the notes are tuned to. If you know the key of the beat, select it here. Auto estimates the key from the vocal only (it cannot hear the beat). \"A?\" with a percentage is an unlocked guess: until a key is locked, notes go to the nearest semitone. A padlock means locked: the singing no longer changes it. Lock now, Re-analyze and Follow key changes are in this menu.",
        "Ton: notaların çekildiği karar sesi. Beat'in tonunu biliyorsan buradan seç. Otomatik, tonu yalnızca vokalden tahmin eder (beat'i duymaz). Yanında yüzde olan \"A?\" henüz kilitlenmemiş bir tahmindir: bir ton kilitlenene kadar notalar en yakın yarım sese çekilir. Asma kilit, tonun kilitlendiğini gösterir: söylenen notalar onu artık değiştirmez. Şimdi kilitle, Yeniden analiz et ve Ton değişimlerini izle bu menüdedir.",
        "Tonart: der Grundton, auf den die Noten gezogen werden. Kennst du die Tonart des Beats, wähle sie hier. Auto schätzt die Tonart nur aus dem Gesang (den Beat hört es nicht). „A?“ mit Prozentwert ist eine noch nicht festgelegte Schätzung: bis eine Tonart festgelegt ist, gehen die Noten zum nächsten Halbton. Ein Schloss bedeutet festgelegt: der Gesang ändert sie nicht mehr. Jetzt festlegen, Neu analysieren und Tonartwechseln folgen findest du in diesem Menü.",
        "Tonalité : la tonique vers laquelle les notes sont corrigées. Si vous connaissez la tonalité du beat, choisissez-la ici. Auto estime la tonalité uniquement d'après la voix (il n'entend pas le beat). « A? » suivi d'un pourcentage est une estimation non verrouillée : tant qu'aucune tonalité n'est verrouillée, les notes vont au demi-ton le plus proche. Un cadenas signifie verrouillée : le chant ne la change plus. Verrouiller maintenant, Réanalyser et Suivre les changements de tonalité sont dans ce menu.",
        "Tonalità: la tonica verso cui vengono corrette le note. Se conosci la tonalità del beat, sceglila qui. Auto stima la tonalità solo dalla voce (non sente il beat). «A?» con una percentuale è una stima non bloccata: finché nessuna tonalità è bloccata, le note vanno al semitono più vicino. Un lucchetto indica che è bloccata: il canto non la cambia più. Blocca ora, Rianalizza e Segui i cambi di tonalità sono in questo menu.",
        "Тональность: тоника, к которой подтягиваются ноты. Если вы знаете тональность бита, выберите её здесь. Авто оценивает тональность только по вокалу (бит она не слышит). «A?» с процентом — ещё не зафиксированная догадка: пока тональность не зафиксирована, ноты идут к ближайшему полутону. Замок означает, что тональность зафиксирована: пение её больше не меняет. «Зафиксировать сейчас», «Анализировать заново» и «Следовать смене тональности» — в этом меню.",
        "调性：音符被校正到的主音。如果你知道伴奏（beat）的调性，请在这里选择。自动模式仅根据人声估计调性（听不到伴奏）。带百分比的“A?”是尚未锁定的猜测：在锁定调性之前，音符校正到最近的半音。锁形图标表示已锁定：演唱不会再改变它。“立即锁定”、“重新分析”和“跟随转调”都在此菜单中。" } },
    { "Scale: which notes are allowed. Chromatic = all 12 notes. Custom scale: pick your own notes.",
      { "Scale: which notes are allowed. Chromatic = all 12 notes. Custom scale: pick your own notes.",
        "Dizi: hangi notalara izin verildiği. Kromatik = 12 notanın tümü. Özel dizi: notaları kendin seç.",
        "Skala: welche Noten erlaubt sind. Chromatisch = alle 12 Töne. Eigene Skala: wähle deine eigenen Noten.",
        "Gamme : les notes autorisées. Chromatique = les 12 notes. Gamme personnalisée : choisissez vos notes.",
        "Scala: quali note sono consentite. Cromatica = tutte le 12 note. Scala personalizzata: scegli le tue note.",
        "Лад: какие ноты разрешены. Хроматическая = все 12 нот. Свой лад: выберите ноты сами.",
        "音阶：允许使用的音符。半音阶 = 全部 12 个音。自定义音阶：自己选择音符。" } },
    { "Pitch Correction: how much of the distance to the target note is corrected.",
      { "Pitch Correction: how much of the distance to the target note is corrected.",
        "Perde düzeltme: hedef notaya olan mesafenin ne kadarının düzeltileceği.",
        "Tonhöhenkorrektur: wie viel des Abstands zur Zielnote korrigiert wird.",
        "Correction : quelle part de l'écart avec la note cible est corrigée.",
        "Correzione: quanta parte della distanza dalla nota di destinazione viene corretta.",
        "Коррекция высоты: какая часть отклонения от целевой ноты исправляется.",
        "音高校正：校正与目标音之间偏差的比例。" } },
    { "Retune Speed: how fast the pitch is pulled to the note. 0 ms = instant (robotic).",
      { "Retune Speed: how fast the pitch is pulled to the note. 0 ms = instant (robotic).",
        "Düzeltme hızı: perdenin notaya ne kadar hızlı çekildiği. 0 ms = anında (robotik).",
        "Korrekturtempo: wie schnell die Tonhöhe zur Note gezogen wird. 0 ms = sofort (robotisch).",
        "Vitesse de correction : rapidité avec laquelle la hauteur rejoint la note. 0 ms = instantané (robotique).",
        "Velocità di correzione: quanto velocemente l'intonazione viene portata sulla nota. 0 ms = istantaneo (robotico).",
        "Скорость коррекции: как быстро высота подтягивается к ноте. 0 ms = мгновенно (роботизированно).",
        "校正速度：音高被拉到目标音的快慢。0 ms = 立即（机械效果）。" } },
    { "Humanize: relaxes the correction on long, sustained notes so they keep their natural movement.",
      { "Humanize: relaxes the correction on long, sustained notes so they keep their natural movement.",
        "Doğallık: uzun tutulan notalarda düzeltmeyi gevşetir, böylece doğal hareketleri korunur.",
        "Humanisieren: lockert die Korrektur auf langen, gehaltenen Noten, damit sie ihre natürliche Bewegung behalten.",
        "Humaniser : assouplit la correction sur les notes longues et tenues pour garder leur mouvement naturel.",
        "Umanizza: allenta la correzione sulle note lunghe e tenute, che conservano il loro movimento naturale.",
        "Естественность: ослабляет коррекцию на длинных нотах, сохраняя их естественное движение.",
        "人性化：在长音上放松校正，保留其自然的音高变化。" } },
    { "Vibrato Scale: depth of the natural vibrato and pitch movement (100% = as sung).",
      { "Vibrato Scale: depth of the natural vibrato and pitch movement (100% = as sung).",
        "Vibrato: doğal vibratonun ve perde hareketinin derinliği (%100 = söylendiği gibi).",
        "Vibrato: Tiefe des natürlichen Vibratos und der Tonhöhenbewegung (100 % = wie gesungen).",
        "Vibrato : profondeur du vibrato naturel et des mouvements de hauteur (100 % = tel que chanté).",
        "Vibrato: profondità del vibrato naturale e del movimento dell'intonazione (100% = come cantato).",
        "Вибрато: глубина естественного вибрато и движения высоты (100% = как спето).",
        "颤音：自然颤音和音高变化的深度（100% = 保持原唱）。" } },
    { "Formant: shifts the vocal character (throat size) in semitones without changing the pitch.",
      { "Formant: shifts the vocal character (throat size) in semitones without changing the pitch.",
        "Formant: perdeyi değiştirmeden ses rengini (gırtlak boyu) yarım ses adımlarıyla kaydırır.",
        "Formant: verschiebt den Stimmcharakter (Rachengröße) in Halbtönen, ohne die Tonhöhe zu ändern.",
        "Formant : déplace le caractère vocal (taille du conduit vocal) en demi-tons sans changer la hauteur.",
        "Formante: sposta il carattere della voce (dimensione della gola) in semitoni senza cambiare l'intonazione.",
        "Форманта: сдвигает тембр голоса (размер гортани) в полутонах, не меняя высоту.",
        "共振峰：以半音为单位改变音色（声道大小），不改变音高。" } },
    { "Pitch Range: transposes the corrected voice by up to 12 semitones up or down.",
      { "Pitch Range: transposes the corrected voice by up to 12 semitones up or down.",
        "Transpoze: düzeltilmiş sesi 12 yarım sese kadar yukarı veya aşağı taşır.",
        "Transponieren: verschiebt die korrigierte Stimme um bis zu 12 Halbtöne nach oben oder unten.",
        "Transposition : transpose la voix corrigée jusqu'à 12 demi-tons vers le haut ou le bas.",
        "Trasposizione: traspone la voce corretta fino a 12 semitoni in su o in giù.",
        "Транспозиция: сдвигает исправленный голос до 12 полутонов вверх или вниз.",
        "移调：将校正后的人声向上或向下移动最多 12 个半音。" } },
    { "Mix: blend between the original and the tuned voice.",
      { "Mix: blend between the original and the tuned voice.",
        "Miks: orijinal ile düzeltilmiş ses arasındaki karışım.",
        "Mix: Mischung zwischen Original- und korrigierter Stimme.",
        "Mix : mélange entre la voix originale et la voix corrigée.",
        "Mix: miscela tra la voce originale e quella corretta.",
        "Микс: баланс между исходным и исправленным голосом.",
        "混合：原声与校正后人声的混合比例。" } },
    { "ROBOTISM: turn left for the hard, robotic effect; turn right for natural, human transitions.",
      { "ROBOTISM: turn left for the hard, robotic effect; turn right for natural, human transitions.",
        "ROBOTISM: sert, robotik efekt için sola; doğal, insansı geçişler için sağa çevirin.",
        "ROBOTISM: nach links für den harten, robotischen Effekt; nach rechts für natürliche, menschliche Übergänge.",
        "ROBOTISM : à gauche pour l'effet robotique dur ; à droite pour des transitions naturelles et humaines.",
        "ROBOTISM: a sinistra per l'effetto robotico duro; a destra per transizioni naturali e umane.",
        "ROBOTISM: влево — жёсткий роботизированный эффект; вправо — естественные, живые переходы.",
        "ROBOTISM：向左转获得强烈的机械效果；向右转获得自然的过渡。" } },
    { "What does ROBOTISM do?",
      { "What does ROBOTISM do?", "ROBOTISM ne yapar?", "Was macht ROBOTISM?", "Que fait ROBOTISM ?",
        "Cosa fa ROBOTISM?", "Что делает ROBOTISM?", "ROBOTISM 有什么作用？" } },
    { "Save preset",
      { "Save preset", "Preseti kaydet", "Preset speichern", "Enregistrer le preset", "Salva preset", "Сохранить пресет", "保存预设" } },
    { "Window size",
      { "Window size", "Pencere boyutu", "Fenstergröße", "Taille de la fenêtre", "Dimensione finestra", "Размер окна", "窗口大小" } },
    { "Settings",
      { "Settings", "Ayarlar", "Einstellungen", "Réglages", "Impostazioni", "Настройки", "设置" } },
    { "Show the raw input pitch in the display",
      { "Show the raw input pitch in the display", "Ekranda ham giriş perdesini göster", "Rohe Eingangstonhöhe in der Anzeige zeigen",
        "Afficher la hauteur d'entrée brute", "Mostra l'intonazione grezza in ingresso", "Показывать исходную высоту входа",
        "在显示区显示原始输入音高" } },
    { "Turn the tuning on / off (follows the host's bypass).",
      { "Turn the tuning on / off (follows the host's bypass).",
        "Düzeltmeyi açar / kapatır (DAW'ın bypass düğmesiyle senkron).",
        "Korrektur ein / aus (folgt dem Bypass des Hosts).",
        "Active / désactive la correction (suit le bypass de l'hôte).",
        "Attiva / disattiva la correzione (segue il bypass dell'host).",
        "Включает / выключает коррекцию (синхронно с bypass хоста).",
        "开启 / 关闭校正（与宿主的旁通同步）。" } },
    { "REALTIME on: low latency (20 ms) for tracking and live use.\nOff: high quality (40 ms), better for deep voices; the host compensates the delay.",
      { "REALTIME on: low latency (20 ms) for tracking and live use.\nOff: high quality (40 ms), better for deep voices; the host compensates the delay.",
        "GERÇEK ZAMAN açık: düşük gecikme (20 ms), kayıt ve canlı kullanım için.\nKapalı: yüksek kalite (40 ms), kalın seslerde daha iyi; DAW gecikmeyi telafi eder.",
        "ECHTZEIT an: geringe Latenz (20 ms) für Aufnahme und Live-Einsatz.\nAus: hohe Qualität (40 ms), besser für tiefe Stimmen; der Host gleicht die Verzögerung aus.",
        "TEMPS RÉEL activé : faible latence (20 ms) pour l'enregistrement et le live.\nDésactivé : haute qualité (40 ms), meilleur pour les voix graves ; l'hôte compense le délai.",
        "TEMPO REALE attivo: bassa latenza (20 ms) per registrazione e uso dal vivo.\nDisattivo: alta qualità (40 ms), migliore per voci gravi; l'host compensa il ritardo.",
        "РЕАЛЬНОЕ ВРЕМЯ вкл.: низкая задержка (20 ms) для записи и живого исполнения.\nВыкл.: высокое качество (40 ms), лучше для низких голосов; хост компенсирует задержку.",
        "实时 开：低延迟（20 ms），适合录音和现场使用。\n关：高质量（40 ms），更适合低沉的嗓音；宿主会补偿延迟。" } },

    //------------------------------------------------------------- menus and dialogs
    { "Factory",            { "Factory", "Fabrika", "Werk", "Usine", "Fabbrica", "Заводские", "出厂" } },
    { "User",               { "User", "Kullanıcı", "Benutzer", "Utilisateur", "Utente", "Пользовательские", "用户" } },
    { "Save preset...",     { "Save preset...", "Preseti kaydet...", "Preset speichern...", "Enregistrer le preset...", "Salva preset...", "Сохранить пресет...", "保存预设..." } },
    { "Delete \"%1\"",      { "Delete \"%1\"", "\"%1\" sil", "\"%1\" löschen", "Supprimer « %1 »", "Elimina \"%1\"", "Удалить «%1»", "删除“%1”" } },
    { "Open preset folder", { "Open preset folder", "Preset klasörünü aç", "Preset-Ordner öffnen", "Ouvrir le dossier des presets", "Apri cartella preset", "Открыть папку пресетов", "打开预设文件夹" } },
    { "Reset to default (%1)", { "Reset to default (%1)", "Varsayılana dön (%1)", "Auf Standard zurücksetzen (%1)", "Valeur par défaut (%1)", "Ripristina predefinito (%1)", "Сбросить по умолчанию (%1)", "恢复默认值（%1）" } },
    { "Small",              { "Small", "Küçük", "Klein", "Petit", "Piccola", "Маленький", "小" } },
    { "Compact",            { "Compact", "Kompakt", "Kompakt", "Compact", "Compatta", "Компактный", "紧凑" } },
    { "Default",            { "Default", "Varsayılan", "Standard", "Par défaut", "Predefinita", "По умолчанию", "默认" } },
    { "Large",              { "Large", "Büyük", "Groß", "Grand", "Grande", "Большой", "大" } },
    { "Full",               { "Full", "Tam", "Voll", "Plein", "Piena", "Полный", "完整" } },
    { "Vocal range",        { "Vocal range", "Ses aralığı", "Stimmlage", "Tessiture", "Estensione vocale", "Диапазон голоса", "声部" } },
    { "Show input pitch",   { "Show input pitch", "Giriş perdesini göster", "Eingangstonhöhe zeigen", "Afficher la hauteur d'entrée", "Mostra intonazione in ingresso", "Показывать входную высоту", "显示输入音高" } },
    { "Reset all parameters", { "Reset all parameters", "Tüm ayarları sıfırla", "Alle Parameter zurücksetzen", "Réinitialiser tous les paramètres", "Ripristina tutti i parametri", "Сбросить все параметры", "重置所有参数" } },
    { "About VSTM Tune",    { "About VSTM Tune", "VSTM Tune hakkında", "Über VSTM Tune", "À propos de VSTM Tune", "Informazioni su VSTM Tune", "О VSTM Tune", "关于 VSTM Tune" } },
    { "about.text",
      { "Real-time vocal pitch correction.\n\nPitch tracking: YIN with sub-sample refinement\nShifting: pitch-synchronous overlap-add (formant-preserving)\nLatency: %1 samples\n\nFont: Inter (SIL Open Font License 1.1)",
        "Gerçek zamanlı vokal perde düzeltme.\n\nPerde algılama: YIN, örnek altı ince ayar\nKaydırma: perdeye senkron üst üste ekleme (formant korumalı)\nGecikme: %1 örnek\n\nYazı tipi: Inter (SIL Open Font License 1.1)",
        "Vokale Tonhöhenkorrektur in Echtzeit.\n\nTonhöhenerkennung: YIN mit Sub-Sample-Verfeinerung\nVerschiebung: tonhöhensynchrones Overlap-Add (formanterhaltend)\nLatenz: %1 Samples\n\nSchrift: Inter (SIL Open Font License 1.1)",
        "Correction de hauteur vocale en temps réel.\n\nDétection : YIN avec affinage sous-échantillon\nDécalage : overlap-add synchrone à la hauteur (préserve les formants)\nLatence : %1 échantillons\n\nPolice : Inter (SIL Open Font License 1.1)",
        "Correzione dell'intonazione vocale in tempo reale.\n\nRilevamento: YIN con affinamento sub-campione\nSpostamento: overlap-add sincrono (preserva le formanti)\nLatenza: %1 campioni\n\nCarattere: Inter (SIL Open Font License 1.1)",
        "Коррекция высоты вокала в реальном времени.\n\nОпределение высоты: YIN с субсэмпловым уточнением\nСдвиг: синхронный с высотой overlap-add (с сохранением формант)\nЗадержка: %1 сэмплов\n\nШрифт: Inter (SIL Open Font License 1.1)",
        "实时人声音高校正。\n\n音高检测：YIN，亚采样精度\n移调：音高同步叠加（保留共振峰）\n延迟：%1 个采样\n\n字体：Inter（SIL Open Font License 1.1）" } },
    { "OK",                 { "OK", "Tamam", "OK", "OK", "OK", "ОК", "确定" } },
    { "robotism.info",
      { "ROBOTISM - character with one knob\n\n◀  Robotic (left): notes snap instantly, like steps, and vibrato is flattened. The classic robotic autotune effect.\n\n▶  Human (right): the correction softens, transitions glide along an S-curve, vibrato and natural slides are kept. Inaudible, natural correction.\n\nCentre: RETUNE SPEED and HUMANIZE apply as set.",
        "ROBOTISM - tek düğmeyle karakter\n\n◀  Robotik (sola): notalar anında ve basamak gibi oturur, vibrato dümdüz olur. Klasik robotik autotune efekti.\n\n▶  İnsansı (sağa): düzeltme yumuşar, notalar arası geçiş S-eğrisiyle kayar, vibrato ve doğal kayışlar korunur. Duyulmayan, doğal düzeltme.\n\nOrtada: DÜZELTME HIZI ve DOĞALLIK ayarları olduğu gibi uygulanır.",
        "ROBOTISM – Charakter mit einem Regler\n\n◀  Robotisch (links): Noten rasten sofort wie Stufen ein, das Vibrato wird glatt. Der klassische robotische Autotune-Effekt.\n\n▶  Menschlich (rechts): die Korrektur wird weicher, Übergänge gleiten in einer S-Kurve, Vibrato und natürliche Schleifer bleiben erhalten. Unhörbare, natürliche Korrektur.\n\nMitte: KORREKTURTEMPO und HUMANISIEREN wirken wie eingestellt.",
        "ROBOTISM – le caractère en un seul bouton\n\n◀  Robotique (à gauche) : les notes se calent instantanément, en escalier, et le vibrato est aplati. L'effet autotune robotique classique.\n\n▶  Humain (à droite) : la correction s'adoucit, les transitions glissent en courbe en S, le vibrato et les glissements naturels sont conservés. Une correction naturelle et inaudible.\n\nAu centre : VITESSE DE CORRECTION et HUMANISER s'appliquent tels quels.",
        "ROBOTISM – il carattere con una sola manopola\n\n◀  Robotico (sinistra): le note si agganciano subito, a gradini, e il vibrato viene appiattito. Il classico effetto autotune robotico.\n\n▶  Umano (destra): la correzione si ammorbidisce, le transizioni scivolano con una curva a S, vibrato e scivolamenti naturali restano. Correzione naturale e impercettibile.\n\nAl centro: VELOCITÀ DI CORREZIONE e UMANIZZA si applicano come impostati.",
        "ROBOTISM — характер одной ручкой\n\n◀  Робот (влево): ноты встают мгновенно, ступеньками, вибрато выравнивается. Классический роботизированный эффект автотюна.\n\n▶  Живой (вправо): коррекция мягче, переходы скользят по S-кривой, вибрато и естественные подъезды сохраняются. Незаметная, естественная коррекция.\n\nПо центру: СКОРОСТЬ КОРРЕКЦИИ и ЕСТЕСТВЕННОСТЬ работают как заданы.",
        "ROBOTISM —— 一个旋钮决定风格\n\n◀  机械（向左）：音符立即像台阶一样对齐，颤音被拉平。经典的机械自动修音效果。\n\n▶  自然（向右）：校正变柔和，音符之间以 S 曲线滑动，保留颤音和自然的滑音。听不出来的自然校正。\n\n居中：校正速度和人性化按设定值生效。" } },
    { "Name of the new preset:", { "Name of the new preset:", "Yeni presetin adı:", "Name des neuen Presets:", "Nom du nouveau preset :", "Nome del nuovo preset:", "Имя нового пресета:", "新预设名称：" } },
    { "My Preset",          { "My Preset", "Presetim", "Mein Preset", "Mon preset", "Il mio preset", "Мой пресет", "我的预设" } },
    { "Save",               { "Save", "Kaydet", "Speichern", "Enregistrer", "Salva", "Сохранить", "保存" } },
    { "Cancel",             { "Cancel", "İptal", "Abbrechen", "Annuler", "Annulla", "Отмена", "取消" } },
    { "Pitch display",      { "Pitch display", "Perde ekranı", "Tonhöhenanzeige", "Affichage de la hauteur", "Display intonazione", "Дисплей высоты", "音高显示" } },
    { "Sing or play audio to see the pitch",
      { "Sing or play audio to see the pitch", "Perdeyi görmek için şarkı söyleyin veya ses çalın", "Singen oder Audio abspielen, um die Tonhöhe zu sehen",
        "Chantez ou lisez de l'audio pour voir la hauteur", "Canta o riproduci audio per vedere l'intonazione", "Спойте или включите аудио, чтобы увидеть высоту",
        "演唱或播放音频以查看音高" } },

    //------------------------------------------------------------- parameter names (host)
    { "Key",              { "Key", "Ton", "Tonart", "Tonalité", "Tonalità", "Тональность", "调性" } },
    { "Scale",            { "Scale", "Dizi", "Skala", "Gamme", "Scala", "Лад", "音阶" } },
    { "Pitch Correction", { "Pitch Correction", "Perde Düzeltme", "Tonhöhenkorrektur", "Correction", "Correzione", "Коррекция высоты", "音高校正" } },
    { "Retune Speed",     { "Retune Speed", "Düzeltme Hızı", "Korrekturtempo", "Vitesse de correction", "Velocità di correzione", "Скорость коррекции", "校正速度" } },
    { "Humanize",         { "Humanize", "Doğallık", "Humanisieren", "Humaniser", "Umanizza", "Естественность", "人性化" } },
    { "Robotism",         { "Robotism", "Robotism", "Robotism", "Robotism", "Robotism", "Robotism", "Robotism" } },
    { "Vibrato Scale",    { "Vibrato Scale", "Vibrato", "Vibrato", "Vibrato", "Vibrato", "Вибрато", "颤音" } },
    { "Formant",          { "Formant", "Formant", "Formant", "Formant", "Formante", "Форманта", "共振峰" } },
    { "Pitch Range",      { "Pitch Range", "Transpoze", "Transponieren", "Transposition", "Trasposizione", "Транспозиция", "移调" } },
    { "Mix",              { "Mix", "Miks", "Mix", "Mix", "Mix", "Микс", "混合" } },
    { "Bypass",           { "Bypass", "Bypass", "Bypass", "Bypass", "Bypass", "Bypass", "旁通" } },
    { "Realtime",         { "Realtime", "Gerçek Zaman", "Echtzeit", "Temps réel", "Tempo reale", "Реальное время", "实时" } },
    { "Vocal Range",      { "Vocal Range", "Ses Aralığı", "Stimmlage", "Tessiture", "Estensione vocale", "Диапазон голоса", "声部" } },
    { "On",               { "On", "Açık", "An", "Activé", "Attivo", "Вкл", "开" } },
    { "Off",              { "Off", "Kapalı", "Aus", "Désactivé", "Disattivo", "Выкл", "关" } },

    //------------------------------------------------------------- choices
    { "Auto",             { "Auto", "Otomatik", "Auto", "Auto", "Auto", "Авто", "自动" } },
    { "Chromatic",        { "Chromatic", "Kromatik", "Chromatisch", "Chromatique", "Cromatica", "Хроматическая", "半音阶" } },
    { "Major",            { "Major", "Majör", "Dur", "Majeur", "Maggiore", "Мажор", "大调" } },
    { "Minor",            { "Minor", "Minör", "Moll", "Mineur", "Minore", "Минор", "小调" } },
    { "Harmonic Minor",   { "Harmonic Minor", "Armonik Minör", "Harmonisch Moll", "Mineur harmonique", "Minore armonica", "Гармонический минор", "和声小调" } },
    { "Melodic Minor",    { "Melodic Minor", "Melodik Minör", "Melodisch Moll", "Mineur mélodique", "Minore melodica", "Мелодический минор", "旋律小调" } },
    { "Major Pentatonic", { "Major Pentatonic", "Majör Pentatonik", "Dur-Pentatonik", "Pentatonique majeure", "Pentatonica maggiore", "Мажорная пентатоника", "大调五声音阶" } },
    { "Minor Pentatonic", { "Minor Pentatonic", "Minör Pentatonik", "Moll-Pentatonik", "Pentatonique mineure", "Pentatonica minore", "Минорная пентатоника", "小调五声音阶" } },
    { "Blues",            { "Blues", "Blues", "Blues", "Blues", "Blues", "Блюз", "布鲁斯" } },
    { "Dorian",           { "Dorian", "Dor", "Dorisch", "Dorien", "Dorica", "Дорийский", "多利亚" } },
    { "Phrygian",         { "Phrygian", "Frig", "Phrygisch", "Phrygien", "Frigia", "Фригийский", "弗里几亚" } },
    { "Lydian",           { "Lydian", "Lidya", "Lydisch", "Lydien", "Lidia", "Лидийский", "利底亚" } },
    { "Mixolydian",       { "Mixolydian", "Miksolidya", "Mixolydisch", "Mixolydien", "Misolidia", "Миксолидийский", "混合利底亚" } },
    { "Hicaz",            { "Hicaz", "Hicaz", "Hicaz", "Hijaz", "Hijaz", "Хиджаз", "希贾兹" } },
    { "Soprano / Alto",   { "Soprano / Alto", "Soprano / Alto", "Sopran / Alt", "Soprano / Alto", "Soprano / Contralto", "Сопрано / Альт", "女高音 / 女低音" } },
    { "Tenor / Mid",      { "Tenor / Mid", "Tenor / Orta", "Tenor / Mittel", "Ténor / Médium", "Tenore / Medio", "Тенор / Средний", "男高音 / 中音" } },
    { "Bass / Baritone",  { "Bass / Baritone", "Bas / Bariton", "Bass / Bariton", "Basse / Baryton", "Basso / Baritono", "Бас / Баритон", "男低音 / 男中音" } },
    //------------------------------------------------------------- custom scale, Adaptive (added after 1.0)
    { "Custom Scale",     { "Custom Scale", "Özel Dizi", "Eigene Skala", "Gamme personnalisée", "Scala personalizzata", "Свой лад", "自定义音阶" } },
    { "Custom Note",      { "Custom Note", "Özel Nota", "Eigene Note", "Note personnalisée", "Nota personalizzata", "Своя нота", "自定义音符" } },
    { "Adaptive",         { "Adaptive", "Uyarlanır", "Adaptiv", "Adaptatif", "Adattivo", "Адаптивно", "自适应" } },
    { "Custom",           { "Custom", "Özel", "Eigene", "Personnalisée", "Personalizzata", "Свой", "自定义" } },
    { "Custom scale…",    { "Custom scale…", "Özel dizi…", "Eigene Skala…", "Gamme personnalisée…", "Scala personalizzata…", "Свой лад…", "自定义音阶…" } },
    { "Edit custom scale…", { "Edit custom scale…", "Özel diziyi düzenle…", "Eigene Skala bearbeiten…", "Modifier la gamme personnalisée…",
                              "Modifica scala personalizzata…", "Изменить свой лад…", "编辑自定义音阶…" } },
    { "CUSTOM SCALE",     { "CUSTOM SCALE", "ÖZEL DİZİ", "EIGENE SKALA", "GAMME PERSONNALISÉE", "SCALA PERSONALIZZATA", "СВОЙ ЛАД", "自定义音阶" } },
    { "Click the notes the voice may be tuned to. They move with the key (%1).",
      { "Click the notes the voice may be tuned to. They move with the key (%1).",
        "Sesin çekilebileceği notalara tıkla. Notalar tonla birlikte kayar (%1).",
        "Klicke die Noten an, auf die gestimmt werden darf. Sie wandern mit der Tonart (%1).",
        "Cliquez les notes vers lesquelles la voix peut être corrigée. Elles suivent la tonalité (%1).",
        "Clicca le note verso cui la voce può essere corretta. Seguono la tonalità (%1).",
        "Отметьте ноты, к которым можно подтягивать голос. Они сдвигаются вместе с тональностью (%1).",
        "点击允许校正到的音符。音符随调性移动（%1）。" } },
    { "Copy from scale",  { "Copy from scale", "Diziden kopyala", "Aus Skala übernehmen", "Copier la gamme", "Copia dalla scala", "Взять из лада", "从音阶复制" } },
    { "ADAPTIVE",         { "ADAPTIVE", "UYARLANIR", "ADAPTIV", "ADAPTATIF", "ADATTIVO", "АДАПТИВНО", "自适应" } },
    { "Adaptive (experimental): follows the performance within fixed limits. A sung vibrato and deliberate slides are kept (not at the robotic end), fast runs are tuned quicker, uncertain pitch is pulled less. Your settings are not changed.",
      { "Adaptive (experimental): follows the performance within fixed limits. A sung vibrato and deliberate slides are kept (not at the robotic end), fast runs are tuned quicker, uncertain pitch is pulled less. Your settings are not changed.",
        "Uyarlanır (deneysel): performansı sabit sınırlar içinde izler. Söylenen vibrato ve bilinçli kaymalar korunur (robotik uçta değil), hızlı geçişler daha çabuk düzeltilir, belirsiz perde daha az çekilir. Ayarların değişmez.",
        "Adaptiv (experimentell): folgt der Darbietung in festen Grenzen. Gesungenes Vibrato und bewusste Slides bleiben erhalten (nicht am robotischen Ende), schnelle Läufe werden schneller korrigiert, unsichere Tonhöhe wird weniger gezogen. Deine Einstellungen bleiben unverändert.",
        "Adaptatif (expérimental) : suit l'interprétation dans des limites fixes. Le vibrato chanté et les glissés voulus sont conservés (sauf à l'extrémité robotique), les traits rapides sont corrigés plus vite, une hauteur incertaine est moins tirée. Vos réglages ne changent pas.",
        "Adattivo (sperimentale): segue l'esecuzione entro limiti fissi. Il vibrato cantato e i glissati voluti vengono mantenuti (non all'estremo robotico), i passaggi veloci vengono corretti più in fretta, un'intonazione incerta viene corretta meno. Le tue impostazioni non cambiano.",
        "Адаптивно (эксперимент): следует за исполнением в заданных пределах. Спетое вибрато и намеренные глиссандо сохраняются (кроме роботизированного края), быстрые пассажи корректируются быстрее, неуверенная высота подтягивается слабее. Ваши настройки не меняются.",
        "自适应（实验性）：在固定范围内跟随演唱。保留演唱的颤音和有意的滑音（机械端除外），快速乐句校正更快，不确定的音高校正更轻。不会改动你的设置。" } },

    //------------------------------------------------------------- Auto key lock, presets keep the key (1.1)
    { "autokey.hint",     { "VOCAL ESTIMATE", "VOKALDEN TAHMİN", "SCHÄTZUNG AUS GESANG", "ESTIMATION VOCALE", "STIMA DALLA VOCE",
                            "ОЦЕНКА ПО ВОКАЛУ", "根据人声估计" } },
    { "autokey.header",   { "Auto key: estimated from the vocal, not the beat", "Otomatik ton: beat'ten değil, vokalden tahmin",
                            "Auto-Tonart: aus dem Gesang geschätzt, nicht aus dem Beat", "Tonalité auto : estimée d'après la voix, pas le beat",
                            "Tonalità auto: stimata dalla voce, non dal beat", "Авто-тональность: оценка по вокалу, а не по биту",
                            "自动调性：根据人声估计，而非伴奏" } },
    { "autokey.listening", { "Listening… not enough singing yet", "Dinliyor… henüz yeterince şarkı yok", "Hört zu … noch zu wenig Gesang",
                             "Écoute… pas encore assez de chant", "In ascolto… ancora poco canto", "Слушаю… пока мало пения", "正在聆听……演唱还不够" } },
    { "autokey.guess",    { "Not locked: best guess %1 (%2). Notes go to the nearest semitone.",
                            "Kilitli değil: en iyi tahmin %1 (%2). Notalar en yakın yarım sese gider.",
                            "Nicht festgelegt: beste Schätzung %1 (%2). Die Noten gehen zum nächsten Halbton.",
                            "Non verrouillée : meilleure estimation %1 (%2). Les notes vont au demi-ton le plus proche.",
                            "Non bloccata: stima migliore %1 (%2). Le note vanno al semitono più vicino.",
                            "Не зафиксирована: лучшая догадка %1 (%2). Ноты идут к ближайшему полутону.",
                            "未锁定：最佳猜测 %1（%2）。音符校正到最近的半音。" } },
    { "autokey.locked",   { "Locked: %1 (%2 when locked). The singing no longer changes it.",
                            "Kilitli: %1 (kilitlenirken %2). Söylenen notalar artık değiştirmez.",
                            "Festgelegt: %1 (%2 beim Festlegen). Der Gesang ändert sie nicht mehr.",
                            "Verrouillée : %1 (%2 au verrouillage). Le chant ne la change plus.",
                            "Bloccata: %1 (%2 al blocco). Il canto non la cambia più.",
                            "Зафиксирована: %1 (%2 при фиксации). Пение её больше не меняет.",
                            "已锁定：%1（锁定时 %2）。演唱不会再改变它。" } },
    { "autokey.notneeded", { "Chromatic: every note is allowed, the key does not matter.", "Kromatik: tüm notalara izin var, ton fark etmez.",
                             "Chromatisch: alle Noten erlaubt, die Tonart spielt keine Rolle.",
                             "Chromatique : toutes les notes sont permises, la tonalité n'a pas d'importance.",
                             "Cromatica: tutte le note sono consentite, la tonalità non conta.",
                             "Хроматическая: разрешены все ноты, тональность не важна.", "半音阶：允许所有音符，调性无关紧要。" } },
    { "autokey.lock",     { "Lock %1 now", "%1 tonunu şimdi kilitle", "%1 jetzt festlegen", "Verrouiller %1 maintenant", "Blocca %1 ora",
                            "Зафиксировать %1 сейчас", "立即锁定 %1" } },
    { "autokey.use",      { "Use %1 (the vocal now suggests it, %2)", "%1 tonuna geç (vokal artık bunu gösteriyor, %2)",
                            "%1 verwenden (der Gesang deutet jetzt darauf hin, %2)", "Utiliser %1 (la voix l'indique maintenant, %2)",
                            "Usa %1 (ora la voce indica questa, %2)", "Перейти на %1 (вокал теперь указывает на неё, %2)", "改用 %1（人声现在指向它，%2）" } },
    { "autokey.reanalyze", { "Re-analyze (forget and listen again)", "Yeniden analiz et (unut ve baştan dinle)",
                             "Neu analysieren (vergessen und neu zuhören)", "Réanalyser (oublier et réécouter)", "Rianalizza (dimentica e riascolta)",
                             "Анализировать заново (забыть и слушать снова)", "重新分析（清除并重新聆听）" } },
    { "autokey.follow",   { "Follow key changes (modulation)", "Ton değişimlerini izle (modülasyon)", "Tonartwechseln folgen (Modulation)",
                            "Suivre les changements de tonalité (modulation)", "Segui i cambi di tonalità (modulazione)",
                            "Следовать смене тональности (модуляция)", "跟随转调（调性变化）" } },
    { "Presets keep key and scale", { "Presets keep key and scale", "Presetler ton ve diziyi korur", "Presets behalten Tonart und Skala",
                                      "Les presets gardent tonalité et gamme", "I preset mantengono tonalità e scala",
                                      "Пресеты сохраняют тональность и лад", "预设保留调性和音阶" } },
    { "Rename \"%1\"…",   { "Rename \"%1\"…", "\"%1\" adını değiştir…", "\"%1\" umbenennen…", "Renommer « %1 »…", "Rinomina \"%1\"…",
                            "Переименовать «%1»…", "重命名“%1”…" } },
    { "Rename preset",    { "Rename preset", "Preseti yeniden adlandır", "Preset umbenennen", "Renommer le preset", "Rinomina preset",
                            "Переименовать пресет", "重命名预设" } },
    { "New name:",        { "New name:", "Yeni ad:", "Neuer Name:", "Nouveau nom :", "Nuovo nome:", "Новое имя:", "新名称：" } },
    { "Rename",           { "Rename", "Yeniden adlandır", "Umbenennen", "Renommer", "Rinomina", "Переименовать", "重命名" } },
    { "A preset with this name already exists.",
      { "A preset with this name already exists.", "Bu adda bir preset zaten var.", "Ein Preset mit diesem Namen gibt es schon.",
        "Un preset de ce nom existe déjà.", "Esiste già un preset con questo nome.", "Пресет с таким именем уже есть.", "已存在同名预设。" } },
    { "Natural",          { "Natural", "Doğal", "Natürlich", "Naturel", "Naturale", "Естественно", "自然" } },
    { "Effects",          { "Effects", "Efektler", "Effekte", "Effets", "Effetti", "Эффекты", "效果" } },
    //------------------------------------------------------------- 1.4: .drp presets, authorization, developer panel
    { "Import preset...",   { "Import preset...", "Preset içe aktar...", "Preset importieren...", "Importer un preset...", "Importa preset...", "Импортировать пресет...", "导入预设..." } },
    { "Import authorization file...", { "Import authorization file...", "Yetki dosyası içe aktar...", "Autorisierungsdatei importieren...", "Importer un fichier d'autorisation...", "Importa file di autorizzazione...", "Импортировать файл авторизации...", "导入授权文件..." } },
    { "Remove authorization", { "Remove authorization", "Yetkiyi kaldır", "Autorisierung entfernen", "Supprimer l'autorisation", "Rimuovi autorizzazione", "Удалить авторизацию", "移除授权" } },
    { "Developer panel",    { "Developer panel", "Geliştirici paneli", "Entwicklerbereich", "Panneau développeur", "Pannello sviluppatore", "Панель разработчика", "开发者面板" } },
    { "DEVELOPER PANEL",    { "DEVELOPER PANEL", "GELİŞTİRİCİ PANELİ", "ENTWICKLERBEREICH", "PANNEAU DÉVELOPPEUR", "PANNELLO SVILUPPATORE", "ПАНЕЛЬ РАЗРАБОТЧИКА", "开发者面板" } },
    { "Close",              { "Close", "Kapat", "Schließen", "Fermer", "Chiudi", "Закрыть", "关闭" } },
    { "dev.peakguard",      { "Output peak guard (-1 dBFS)", "Çıkış tepe koruması (-1 dBFS)", "Ausgangs-Spitzenbegrenzer (-1 dBFS)", "Protection des crêtes en sortie (-1 dBFS)", "Protezione picchi in uscita (-1 dBFS)", "Защита пиков на выходе (-1 dBFS)", "输出峰值保护（-1 dBFS）" } },
    { "dev.backdate",       { "HQ hard tune: backdated note switch", "HQ hard tune: geri tarihli nota değişimi", "HQ Hard Tune: rückdatierter Notenwechsel", "HQ hard tune : changement de note antidaté", "HQ hard tune: cambio nota retrodatato", "HQ hard tune: смена ноты задним числом", "HQ 硬校正：回溯音符切换" } },
    { "dev.fresh",          { "REALTIME: fresh analysis", "GERÇEK ZAMAN: güncel analiz", "ECHTZEIT: aktuelle Analyse", "TEMPS RÉEL : analyse récente", "TEMPO REALE: analisi aggiornata", "РЕАЛЬНОЕ ВРЕМЯ: свежий анализ", "实时：最新分析" } },
    { "dev.swingkeep",      { "Natural side: keep retune speed in vibrato", "İnsansı taraf: vibratoda düzeltme hızını koru", "Natürliche Seite: Korrekturtempo im Vibrato halten", "Côté naturel : garder la vitesse de correction dans le vibrato", "Lato naturale: mantieni la velocità di correzione nel vibrato", "Естественная сторона: сохранять скорость коррекции в вибрато", "自然端：颤音中保持校正速度" } },
    { "dev.note",
      { "Expert switches are for development: they are not saved and reset when the plug-in reloads or the authorization ends.",
        "Uzman anahtarlar geliştirme içindir: kaydedilmez; eklenti yeniden yüklenince ya da yetki bitince sıfırlanır.",
        "Die Expertenschalter dienen der Entwicklung: Sie werden nicht gespeichert und beim Neuladen des Plug-ins oder bei Ende der Autorisierung zurückgesetzt.",
        "Les commutateurs experts servent au développement : ils ne sont pas enregistrés et se réinitialisent au rechargement du plug-in ou à la fin de l'autorisation.",
        "Gli interruttori esperti servono allo sviluppo: non vengono salvati e si azzerano quando il plug-in viene ricaricato o l'autorizzazione termina.",
        "Экспертные переключатели предназначены для разработки: они не сохраняются и сбрасываются при перезагрузке плагина или окончании авторизации.",
        "专家开关用于开发：不会被保存，插件重新加载或授权结束时会复位。" } },
    { "auth.active",        { "Admin authorization active (%1)", "Admin yetkisi etkin (%1)", "Admin-Autorisierung aktiv (%1)", "Autorisation admin active (%1)", "Autorizzazione admin attiva (%1)", "Авторизация администратора активна (%1)", "管理员授权已启用（%1）" } },
    { "auth.noexpiry",      { "no expiry", "süresiz", "unbefristet", "sans expiration", "senza scadenza", "бессрочно", "永久有效" } },
    { "auth.until",         { "valid until %1", "%1 tarihine kadar geçerli", "gültig bis %1", "valable jusqu'au %1", "valida fino al %1", "действует до %1", "有效期至 %1" } },
    { "Authorization",      { "Authorization", "Yetki", "Autorisierung", "Autorisation", "Autorizzazione", "Авторизация", "授权" } },
    { "auth.ok",
      { "Admin authorization accepted. The developer panel is in the settings menu.",
        "Admin yetkisi kabul edildi. Geliştirici paneli ayarlar menüsünde.",
        "Admin-Autorisierung akzeptiert. Der Entwicklerbereich ist im Einstellungsmenü.",
        "Autorisation admin acceptée. Le panneau développeur se trouve dans le menu des réglages.",
        "Autorizzazione admin accettata. Il pannello sviluppatore è nel menu delle impostazioni.",
        "Авторизация администратора принята. Панель разработчика находится в меню настроек.",
        "管理员授权已接受。开发者面板位于设置菜单中。" } },
    { "auth.removed",       { "The authorization was removed from this computer.", "Yetki bu bilgisayardan kaldırıldı.", "Die Autorisierung wurde von diesem Computer entfernt.", "L'autorisation a été supprimée de cet ordinateur.", "L'autorizzazione è stata rimossa da questo computer.", "Авторизация удалена с этого компьютера.", "已从此电脑移除授权。" } },
    { "auth.error",         { "This authorization file was not accepted: %1", "Bu yetki dosyası kabul edilmedi: %1", "Diese Autorisierungsdatei wurde nicht akzeptiert: %1", "Ce fichier d'autorisation n'a pas été accepté : %1", "Questo file di autorizzazione non è stato accettato: %1", "Этот файл авторизации не принят: %1", "此授权文件未被接受：%1" } },
    { "Import preset",      { "Import preset", "Preset içe aktar", "Preset importieren", "Importer un preset", "Importa preset", "Импорт пресета", "导入预设" } },
    { "preset.imported",    { "Preset imported as \"%1\".", "Preset \"%1\" adıyla içe aktarıldı.", "Preset als „%1“ importiert.", "Preset importé sous le nom « %1 ».", "Preset importato come «%1».", "Пресет импортирован как «%1».", "预设已导入为“%1”。" } },
    { "preset.error",       { "This preset file was not accepted: %1", "Bu preset dosyası kabul edilmedi: %1", "Diese Preset-Datei wurde nicht akzeptiert: %1", "Ce fichier de preset n'a pas été accepté : %1", "Questo file preset non è stato accettato: %1", "Этот файл пресета не принят: %1", "此预设文件未被接受：%1" } },
    { "drp.cannotRead",     { "the file could not be read.", "dosya okunamadı.", "die Datei konnte nicht gelesen werden.", "le fichier n'a pas pu être lu.", "impossibile leggere il file.", "файл не удалось прочитать.", "无法读取该文件。" } },
    { "drp.tooLarge",       { "the file is too large.", "dosya çok büyük.", "die Datei ist zu groß.", "le fichier est trop volumineux.", "il file è troppo grande.", "файл слишком большой.", "文件过大。" } },
    { "drp.damaged",        { "the file is damaged.", "dosya bozuk.", "die Datei ist beschädigt.", "le fichier est endommagé.", "il file è danneggiato.", "файл повреждён.", "文件已损坏。" } },
    { "drp.notDrp",         { "this is not a VSTM Tune .drp file.", "bu bir VSTM Tune .drp dosyası değil.", "dies ist keine VSTM Tune .drp-Datei.", "ce n'est pas un fichier .drp de VSTM Tune.", "questo non è un file .drp di VSTM Tune.", "это не файл .drp VSTM Tune.", "这不是 VSTM Tune 的 .drp 文件。" } },
    { "drp.newer",          { "the file was made by a newer version.", "dosya daha yeni bir sürümle yapılmış.", "die Datei stammt von einer neueren Version.", "le fichier a été créé par une version plus récente.", "il file è stato creato da una versione più recente.", "файл создан более новой версией.", "该文件由更新的版本创建。" } },
    { "drp.isAuth",         { "this is an authorization file; use \"Import authorization file\".", "bu bir yetki dosyası; \"Yetki dosyası içe aktar\"ı kullan.", "dies ist eine Autorisierungsdatei; verwende „Autorisierungsdatei importieren“.", "c'est un fichier d'autorisation ; utilisez « Importer un fichier d'autorisation ».", "questo è un file di autorizzazione; usa «Importa file di autorizzazione».", "это файл авторизации; используйте «Импортировать файл авторизации».", "这是授权文件；请使用“导入授权文件”。" } },
    { "drp.isPreset",       { "this is a preset, not an authorization file.", "bu bir preset, yetki dosyası değil.", "dies ist ein Preset, keine Autorisierungsdatei.", "c'est un preset, pas un fichier d'autorisation.", "questo è un preset, non un file di autorizzazione.", "это пресет, а не файл авторизации.", "这是预设，不是授权文件。" } },
    { "drp.invalid",        { "the contents are invalid.", "içerik geçersiz.", "der Inhalt ist ungültig.", "le contenu n'est pas valide.", "il contenuto non è valido.", "содержимое недействительно.", "内容无效。" } },
    { "drp.wrongPlugin",    { "the file was made for another plug-in.", "dosya başka bir eklenti için yapılmış.", "die Datei wurde für ein anderes Plug-in erstellt.", "le fichier a été créé pour un autre plug-in.", "il file è stato creato per un altro plug-in.", "файл создан для другого плагина.", "该文件是为其他插件创建的。" } },
    { "drp.unknownKey",     { "it is signed with a key this version does not trust.", "bu sürümün güvenmediği bir anahtarla imzalanmış.", "sie ist mit einem Schlüssel signiert, dem diese Version nicht vertraut.", "il est signé avec une clé que cette version ne reconnaît pas.", "è firmato con una chiave non attendibile per questa versione.", "он подписан ключом, которому эта версия не доверяет.", "它使用了此版本不信任的密钥签名。" } },
    { "drp.badSignature",   { "the signature is not valid (the file was changed or forged).", "imza geçersiz (dosya değiştirilmiş ya da sahte).", "die Signatur ist ungültig (die Datei wurde verändert oder gefälscht).", "la signature n'est pas valide (fichier modifié ou falsifié).", "la firma non è valida (file modificato o contraffatto).", "подпись недействительна (файл изменён или подделан).", "签名无效（文件被修改或伪造）。" } },
    { "drp.role",           { "it grants a role this version does not support.", "bu sürümün desteklemediği bir rol veriyor.", "sie gewährt eine Rolle, die diese Version nicht unterstützt.", "il accorde un rôle que cette version ne prend pas en charge.", "concede un ruolo non supportato da questa versione.", "он предоставляет роль, которую эта версия не поддерживает.", "它授予了此版本不支持的角色。" } },
    { "drp.revoked",        { "this authorization has been revoked.", "bu yetki iptal edilmiş.", "diese Autorisierung wurde widerrufen.", "cette autorisation a été révoquée.", "questa autorizzazione è stata revocata.", "эта авторизация отозвана.", "此授权已被撤销。" } },
    { "drp.notYet",         { "it is not valid yet.", "henüz geçerli değil.", "sie ist noch nicht gültig.", "il n'est pas encore valide.", "non è ancora valida.", "он ещё не действует.", "尚未生效。" } },
    { "drp.expired",        { "it has expired.", "süresi dolmuş.", "sie ist abgelaufen.", "il a expiré.", "è scaduta.", "срок его действия истёк.", "已过期。" } },
};
// clang-format on

const std::unordered_map<std::string_view, const Row*>& index()
{
    static const auto map = []
    {
        std::unordered_map<std::string_view, const Row*> m;
        for (const auto& r : rows)
            m.emplace (r.key, &r);
        return m;
    }();
    return map;
}

std::atomic<int>& currentSlot()
{
    static std::atomic<int> slot { (int) detect() };
    return slot;
}
} // namespace

Lang fromCode (const juce::String& c) noexcept
{
    const auto primary = c.trim().toLowerCase().replaceCharacter ('_', '-').upToFirstOccurrenceOf ("-", false, false);
    if (primary == "tr") return Lang::turkish;
    if (primary == "de") return Lang::german;
    if (primary == "fr") return Lang::french;
    if (primary == "it") return Lang::italian;
    if (primary == "ru") return Lang::russian;
    if (primary == "zh") return Lang::chinese;
    return Lang::english;
}

Lang detect()
{
    if (const char* forced = std::getenv ("VSTM_TUNE_LANG"))
        if (*forced != 0)
            return fromCode (juce::String::fromUTF8 (forced));

    const auto display = juce::SystemStats::getDisplayLanguage();
    return fromCode (display.isNotEmpty() ? display : juce::SystemStats::getUserLanguage());
}

Lang current() noexcept               { return (Lang) currentSlot().load (std::memory_order_relaxed); }
void setCurrent (Lang l) noexcept     { currentSlot().store ((int) l, std::memory_order_relaxed); }

const char* code (Lang l) noexcept
{
    static const char* codes[] = { "en", "tr", "de", "fr", "it", "ru", "zh" };
    return codes[(int) l];
}

juce::String tr (const char* key, Lang l)
{
    const auto& m = index();
    const auto it = m.find (key);
    if (it == m.end())
        return juce::String::fromUTF8 (key);
    const char* t = it->second->text[(int) l];
    return juce::String::fromUTF8 (t != nullptr && *t != 0 ? t : it->second->text[0]);
}

juce::String tr (const char* key)                                    { return tr (key, current()); }
juce::String trf (const char* key, const juce::String& arg)          { return tr (key).replace ("%1", arg); }

juce::StringArray allTexts (const char* key)
{
    juce::StringArray s;
    for (int l = 0; l < kNumLangs; ++l)
        s.addIfNotAlreadyThere (tr (key, (Lang) l));
    return s;
}

juce::String upper (const juce::String& s)
{
    if (current() == Lang::turkish)
        return s.replace (juce::String::fromUTF8 ("i"), juce::String::fromUTF8 ("İ"))
                .replace (juce::String::fromUTF8 ("ı"), "I")
                .toUpperCase();
    return s.toUpperCase();
}

int numEntries() noexcept { return (int) (sizeof (rows) / sizeof (rows[0])); }

juce::String firstIncompleteEntry()
{
    for (const auto& r : rows)
        for (int l = 0; l < kNumLangs; ++l)
            if (r.text[l] == nullptr || *r.text[l] == 0)
                return juce::String::fromUTF8 (r.key) + " [" + code ((Lang) l) + "]";
    if ((int) index().size() != numEntries())
        return "duplicate key";
    return {};
}
} // namespace vstm::i18n
