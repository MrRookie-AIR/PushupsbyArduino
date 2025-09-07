#!/usr/bin/env python3
# queue_to_arduino.py

import os
import time
import json
import serial
import requests
import pymysql
from serial.tools import list_ports

# --- Настройки ---
CMD_FILE       = "/var/www/html/iva/pushup_cmd.txt"
BUSY_FLAG      = "/var/www/html/iva/pushup_busy.lock"
SYMLINK_PORT   = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
BAUD_RATE      = 9600
POLL_INTERVAL  = 1
PHP_ENDPOINT   = "http://192.168.11.66/engine/modules/tech/ask_pay_bill1.php"

DB_HOST  = "localhost"
DB_USER  = "root"
DB_PASS  = "tarhush"
DB_NAME  = "cw"

def log(msg):
    ts = time.strftime('%Y-%m-%d %H:%M:%S')
    print(f"{ts} {msg}", flush=True)

def find_serial_port():
    if os.path.exists(SYMLINK_PORT):
        return SYMLINK_PORT
    for p in list_ports.comports():
        if p.device.startswith("/dev/ttyUSB") or p.device.startswith("/dev/ttyACM"):
            log(f"ℹ️ Автоподбор порта: {p.device}")
            return p.device
    return None

def open_serial():
    port = find_serial_port()
    if not port:
        raise RuntimeError(f"Serial port not found: {SYMLINK_PORT} and no USB/ACM ports")
    ser = serial.Serial(port, BAUD_RATE, timeout=1)
    time.sleep(2)  # дать Arduino время на перезагрузку
    log(f"✅ Serial открыт: {port}@{BAUD_RATE}")
    return ser

def get_child_from_rule(rule_id):
    try:
        conn = pymysql.connect(host=DB_HOST, user=DB_USER, password=DB_PASS,
                               db=DB_NAME, charset="utf8mb4")
        with conn.cursor() as cur:
            cur.execute("SELECT user_id FROM rules WHERE id=%s LIMIT 1", (int(rule_id),))
            row = cur.fetchone()
            return row[0] if row else None
    except Exception as e:
        log(f"❌ get_child_from_rule: {e}")
        return None

def get_violation_by_rule(rule_id):
    try:
        conn = pymysql.connect(host=DB_HOST, user=DB_USER, password=DB_PASS,
                               db=DB_NAME, charset="utf8mb4")
        with conn.cursor() as cur:
            cur.execute("""
                SELECT id
                FROM violation
                WHERE rule_id=%s
                  AND date_paid='0000-00-00 00:00:00'
                ORDER BY date_creation DESC
                LIMIT 1
            """, (int(rule_id),))
            row = cur.fetchone()
            return row[0] if row else None
    except Exception as e:
        log(f"❌ get_violation_by_rule: {e}")
        return None

def get_pushup_data(child_id):
    """
    Возвращает кортеж (push_ups, rest_time) для заданного child_id
    из таблицы parents, беря самую свежую запись.
    """
    try:
        conn = pymysql.connect(host=DB_HOST, user=DB_USER, password=DB_PASS,
                               db=DB_NAME, charset="utf8mb4")
        with conn.cursor() as cur:
            cur.execute("""
                SELECT push_ups, time
                FROM parents
                WHERE p_id=10 AND ch_id=%s
                ORDER BY id DESC
                LIMIT 1
            """, (int(child_id),))
            row = cur.fetchone()
            if row:
                return str(row[0]), str(row[1])
            else:
                return "20", "20"
    except Exception as e:
        log(f"❌ get_pushup_data: {e}")
        return "20", "20"

def call_php_payment_by_rule(rule_id):
    viol_id = get_violation_by_rule(rule_id)
    if not viol_id:
        log(f"⚠️ Нет неоплаченных нарушений для rule_id={rule_id}")
        return
    child_id = get_child_from_rule(rule_id)
    if not child_id:
        log(f"⚠️ Не найден child для rule_id={rule_id}")
        return
    try:
        resp = requests.get(PHP_ENDPOINT, params={
            "viol_id": viol_id,
            "t_or_f":  "1",
            "user_id": child_id
        }, timeout=5)
        log(f"💰 PHP платёж для child_id={child_id}: {resp.text.strip()}")
    except Exception as e:
        log(f"❌ call_php_payment_by_rule: {e}")

def mark_violation_by_rule(rule_id):
    viol_id = get_violation_by_rule(rule_id)
    if not viol_id:
        log(f"⚠️ Нет активных нарушений для rule_id={rule_id}")
        return
    try:
        conn = pymysql.connect(host=DB_HOST, user=DB_USER, password=DB_PASS,
                               db=DB_NAME, charset="utf8mb4")
        with conn.cursor() as cur:
            now = time.strftime('%Y-%m-%d %H:%M:%S')
            cur.execute("""
                UPDATE violation
                SET date_paid=%s
                WHERE id=%s
                  AND date_paid='0000-00-00 00:00:00'
            """, (now, viol_id))
            conn.commit()
            log(f"📌 violation {viol_id} (rule_id={rule_id}) отмечено как оплачено ({now})")
    except Exception as e:
        log(f"❌ mark_violation_by_rule: {e}")

def is_rule_paid(rule_id):
    """
    Проверяет, оплачено ли нарушение по rule_id вручную
    (date_paid != '0000-00-00 00:00:00').
    """
    try:
        conn = pymysql.connect(host=DB_HOST, user=DB_USER, password=DB_PASS,
                               db=DB_NAME, charset="utf8mb4")
        with conn.cursor() as cur:
            cur.execute("""
                SELECT date_paid
                FROM violation
                WHERE rule_id=%s
                ORDER BY date_creation DESC
                LIMIT 1
            """, (int(rule_id),))
            row = cur.fetchone()
            return bool(row and row[0] != "0000-00-00 00:00:00")
    except Exception as e:
        log(f"❌ is_rule_paid: {e}")
        return False

def main():
    ser = open_serial()
    busy = False
    send_time = None
    current_rule = None
    check_paid_counter = 0

    while True:
        if not busy:
            if not os.path.exists(CMD_FILE):
                time.sleep(POLL_INTERVAL)
                continue

            with open(CMD_FILE, "r", encoding="utf-8") as f:
                raw = f.read().strip()
            os.remove(CMD_FILE)
            if not raw:
                continue

            try:
                rule_id, user_name = raw.split("|", 1)
            except ValueError:
                log(f"⚠️ Неверная команда: '{raw}'")
                continue

            child_id = get_child_from_rule(rule_id)
            if not child_id:
                log(f"⚠️ Нет ребёнка для rule_id={rule_id}")
                continue

            pushups, rest_time = get_pushup_data(child_id)
            cmd = f"{child_id}|{user_name}|{pushups}|{rest_time}"
            log(f"📤 Шлём Arduino: {cmd}")
            ser.write((cmd + "\n").encode())
            ser.flush()

            busy = True
            send_time = time.time()
            current_rule = rule_id
            check_paid_counter = 0

            with open(BUSY_FLAG, "w") as f:
                f.write("locked")

        else:
            raw = ser.readline().decode('utf-8', errors='ignore').strip()
            check_paid_counter += 1

            # Если каждые 5 циклов обнаружили, что нарушение уже оплачено
            if check_paid_counter % 5 == 0 and current_rule and is_rule_paid(current_rule):
                log(f"🧾 Нарушение rule_id={current_rule} оплачено вручную — снимаем флаг")
                busy = False
                current_rule = None
                if os.path.exists(BUSY_FLAG):
                    os.remove(BUSY_FLAG)
                    log("🧹 Флаг снят вручную")
                continue

            if not raw:
                time.sleep(POLL_INTERVAL)
                continue

            log(f"📮 Received: {raw!r}")

            if raw.startswith("{") and raw.endswith("}"):
                try:
                    obj = json.loads(raw)
                except json.JSONDecodeError:
                    log("⚠️ Невалидный JSON от Arduino")
                    continue

                act = obj.get("action")
                uid = str(obj.get("user_id", ""))
                cnt = obj.get("count", 0)

                if act == "DONE":
                    delta = time.time() - send_time
                    log(f"⏱️ Ответ через {delta:.2f}s")
                    if delta < 2:
                        log("⚠️ Слишком быстро, пропускаем DONE")
                    else:
                        log(f"✅ rule_id={current_rule}, child_id={uid} сделал {cnt} отжиманий")
                        call_php_payment_by_rule(current_rule)
                        mark_violation_by_rule(current_rule)
                        busy = False
                        current_rule = None
                        if os.path.exists(BUSY_FLAG):
                            os.remove(BUSY_FLAG)
                            log("🧹 Флаг снят после DONE")

                elif act == "🌀 AUTO_START":
                    log("ℹ️ Авто-серия — игнорируем")

                else:
                    log(f"ℹ️ Неизвестный action='{act}'")

            elif raw.startswith("ERR"):
                log(f"❗️ Arduino Error: {raw}")

            elif raw.startswith("ACK|START"):
                log(f"🔄 ACK от Arduino: {raw}")

            else:
                log(f"ℹ️ Нераспознанная строка: '{raw}'")

if __name__ == "__main__":
    main()
