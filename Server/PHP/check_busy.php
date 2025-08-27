<?php
// check_busy.php — проверка занятости отжиманий
header('Content-Type: text/plain; charset=utf-8');

// путь к флагу занятости
$flag = '/tmp/pushup_busy.lock';

// если флаг не существует — всё готово
if (!file_exists($flag)) {
  echo 'READY';
  exit;
}

// проверка возраста флага
$max_age = 300; // 5 минут
$age = time() - filemtime($flag);

if ($age > $max_age) {
  // флаг устарел — удаляем
  unlink($flag);
  error_log("⚠️ Удалён устаревший флаг BUSY (age={$age}s)");
  echo 'READY';
  exit;
}

// флаг актуален — кто-то делает отжимания
echo 'BUSY';