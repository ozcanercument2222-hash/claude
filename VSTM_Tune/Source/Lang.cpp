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
    { "Key: the tonic notes are snapped to. Auto listens to the singing and finds it; a question mark means it is not sure yet, until then notes go to the nearest semitone.",
      { "Key: the tonic notes are snapped to. Auto listens to the singing and finds it; a question mark means it is not sure yet, until then notes go to the nearest semitone.",
        "Ton: notaların çekildiği karar sesi. Otomatik, şarkıyı dinleyip tonu kendisi bulur; soru işareti henüz emin olmadığını gösterir, o zamana kadar notalar en yakın yarım sese çekilir.",
        "Tonart: der Grundton, auf den die Noten gezogen werden. Auto hört zu und erkennt ihn selbst; ein Fragezeichen bedeutet „noch nicht sicher“, bis dahin gehen die Noten zum nächsten Halbton.",
        "Tonalité : la tonique vers laquelle les notes sont corrigées. Auto écoute le chant et la trouve ; un point d'interrogation signifie « pas encore sûr », d'ici là les notes vont au demi-ton le plus proche.",
        "Tonalità: la tonica verso cui vengono corrette le note. Auto ascolta il canto e la trova; un punto interrogativo significa «non ancora sicuro», fino ad allora le note vanno al semitono più vicino.",
        "Тональность: тоника, к которой подтягиваются ноты. Авто слушает пение и определяет её сама; вопросительный знак означает, что она ещё не определена надёжно, до тех пор ноты идут к ближайшему полутону.",
        "调性：音符被校正到的主音。自动模式会聆听演唱并自行识别；问号表示尚不确定，在此之前音符校正到最近的半音。" } },
    { "Scale: which notes are allowed. Chromatic = all 12 notes.",
      { "Scale: which notes are allowed. Chromatic = all 12 notes.",
        "Dizi: hangi notalara izin verildiği. Kromatik = 12 notanın tümü.",
        "Skala: welche Noten erlaubt sind. Chromatisch = alle 12 Töne.",
        "Gamme : les notes autorisées. Chromatique = les 12 notes.",
        "Scala: quali note sono consentite. Cromatica = tutte le 12 note.",
        "Лад: какие ноты разрешены. Хроматическая = все 12 нот.",
        "音阶：允许使用的音符。半音阶 = 全部 12 个音。" } },
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
