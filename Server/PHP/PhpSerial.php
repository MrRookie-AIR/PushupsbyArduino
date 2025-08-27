<?php
class PhpSerial {
    var $_device = null;
    var $_baudRate = 9600;
    var $_handle = null;

    function __construct() {
        // Конструктор по-современному
    }

    function deviceSet($device) {
        $this->_device = $device;
    }

    function confBaudRate($rate) {
        $this->_baudRate = $rate;
    }

    function deviceOpen() {
        if (!$this->_device) {
            throw new Exception("❌ Устройство не задано");
        }

        // Проверка наличия stty
        $sttyPath = trim(shell_exec("which stty"));
        if (!$sttyPath) {
            throw new Exception("❌ Команда stty не найдена");
        }

        // Настройка порта
        $cmd = $sttyPath . " -F " . $this->_device . " " . $this->_baudRate;
        exec($cmd, $output, $resultCode);
        if ($resultCode !== 0) {
            throw new Exception("❌ Не удалось настроить порт: " . implode("\n", $output));
        }

        $this->_handle = fopen($this->_device, "w+");
        if (!$this->_handle) {
            throw new Exception("❌ Не удалось открыть устройство");
        }
    }

    function sendMessage($message) {
        if (!$this->_handle) {
            throw new Exception("❌ Устройство не открыто");
        }
        fwrite($this->_handle, $message);
    }

    function deviceClose() {
        if ($this->_handle) {
            fclose($this->_handle);
            $this->_handle = null;
        }
    }
}
?>