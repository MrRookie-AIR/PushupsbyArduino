<?php
// add_to_queue.php — записываем rule_id вместо user_id

$base       = __DIR__;
$cmd_file   = "$base/pushup_cmd.txt";
$busy_flag  = "$base/pushup_busy.lock";
$debug_file = "$base/debug.log";
$history_log = "$base/history.log";
$flag_log    = "$base/pushup_flag.log";

// Параметры подключения к БД
$DB_HOST = "localhost";
$DB_USER = "root";
$DB_PASS = "tarhush";
$DB_NAME = "cw";

// Сбрасываем кеши
clearstatcache(true, $cmd_file);
clearstatcache(true, $busy_flag);

// Функция логирования
function debug_log($msg) {
    global $debug_file;
    file_put_contents(
        $debug_file,
        date('Y-m-d H:i:s') . "  $msg\n",
        FILE_APPEND | LOCK_EX
    );
}

// Логируем текущее состояние
debug_log(sprintf(
    "🔍 Проверка состояния: cmd_exists=%d, busy_exists=%d",
    file_exists($cmd_file) ? 1 : 0,
    file_exists($busy_flag) ? 1 : 0
));

// Получаем параметры
$rule_id   = isset($_GET['rule_id']) ? trim($_GET['rule_id']) : '';
$user_id   = isset($_GET['user_id']) ? trim($_GET['user_id']) : '';
$user_name = isset($_GET['name'])    ? strip_tags(trim($_GET['name'])) : '';

// 1) Нельзя поставить новую команду, пока старая не обработана
if (file_exists($cmd_file)) {
    debug_log("⏳ Блокировка: команда в очереди ещё не обработана");
    exit("⏳ Уже есть команда в очереди. Подождите окончание текущей серии.");
}

// 2) Фоллбэк: если rule_id не передан, но указан user_id
if ($rule_id === '' && $user_id !== '') {
    $mysqli = new mysqli($DB_HOST, $DB_USER, $DB_PASS, $DB_NAME);
    if ($mysqli->connect_errno) {
        debug_log("❌ Ошибка подключения к БД: {$mysqli->connect_error}");
        exit("❌ Ошибка подключения к БД");
    }

    $stmt = $mysqli->prepare("
        SELECT id
          FROM rules
         WHERE user_id = ?
         ORDER BY id DESC
         LIMIT 1
    ");
    if (!$stmt) {
        debug_log("❌ Ошибка подготовки запроса: {$mysqli->error}");
        exit("❌ Ошибка запроса к БД");
    }

    $stmt->bind_param('i', $user_id);
    $stmt->execute();
    $stmt->bind_result($found_rule);
    $stmt->fetch();
    $stmt->close();
    $mysqli->close();

    if ($found_rule) {
        $rule_id = $found_rule;
        debug_log("ℹ️ Фоллбэк: по user_id={$user_id} найден rule_id={$rule_id}");
    } else {
        debug_log("❌ Не найден rule_id по user_id={$user_id}");
        exit("❌ Не удалось найти rule_id для пользователя");
    }
}

// 3) Проверка обязательных параметров
if ($rule_id === '' || $user_name === '') {
    debug_log("❌ Отсутствуют обязательные параметры: rule_id='{$rule_id}', name='{$user_name}'");
    exit("❌ Параметры rule_id и name обязательны");
}

// 4) Проверка занятости
if (file_exists($busy_flag)) {
    debug_log("⏳ Блокировка: серия в работе (флаг занятости активен)");
    exit("⏳ Сейчас серия в работе. Подождите немного.");
}

// 5) Запись команды
$line = "{$rule_id}|{$user_name}";
debug_log("✏️ Пишем команду: {$line}");
if (file_put_contents($cmd_file, $line, LOCK_EX) === false) {
    debug_log("❌ Не удалось записать {$cmd_file}");
    exit("❌ Ошибка записи команды на сервере");
}

// 6) Установка флага занятости
if (file_put_contents($busy_flag, time(), LOCK_EX) === false) {
    debug_log("⚠️ Команда записана, но не удалось создать флаг занятости {$busy_flag}");
} else {
    debug_log("✅ Флаг занятости установлен");
    file_put_contents(
        $flag_log,
        date('Y-m-d H:i:s') . " → Флаг установлен: rule_id={$rule_id}, name={$user_name}\n",
        FILE_APPEND | LOCK_EX
    );
}

// 7) Запись в историю
file_put_contents(
    $history_log,
    date('Y-m-d H:i:s') . " → rule_id={$rule_id}, name={$user_name}\n",
    FILE_APPEND | LOCK_EX
);

echo "✅ Команда отправлена: {$user_name} (rule_id: {$rule_id})";