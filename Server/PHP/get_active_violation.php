<?php
// get_active_violation.php — возвращает актуальный rule_id по user_id

// Подключение конфигурации
$DB_HOST = "localhost";
$DB_USER = "root";
$DB_PASS = "tarhush";
$DB_NAME = "cw";

// Получаем user_id
$user_id = isset($_GET['user_id']) ? intval($_GET['user_id']) : 0;
if (!$user_id) {
    error_log("⚠️ Не передан user_id");
    exit("0");
}

// Подключение к БД
$mysqli = new mysqli($DB_HOST, $DB_USER, $DB_PASS, $DB_NAME);
if ($mysqli->connect_errno) {
    error_log("❌ Ошибка подключения к БД: {$mysqli->connect_error}");
    exit("0");
}

// Подготовка запроса
$stmt = $mysqli->prepare("
    SELECT v.rule_id
      FROM violation v
      JOIN rules r ON r.id = v.rule_id
     WHERE r.user_id = ?
       AND v.date_paid = '0000-00-00 00:00:00'
     ORDER BY v.date_creation ASC
     LIMIT 1
");

if (!$stmt) {
    error_log("❌ Ошибка подготовки запроса: {$mysqli->error}");
    $mysqli->close();
    exit("0");
}

// Выполнение запроса
$stmt->bind_param('i', $user_id);
$stmt->execute();
$stmt->bind_result($rule_id);
$stmt->fetch();
$stmt->close();
$mysqli->close();

// Возврат результата
echo $rule_id ?: "0";