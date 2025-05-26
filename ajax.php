<?php
/* AJAX Handler for Arduino Serial Controller */
header('Content-Type: application/json');

$plugin = "arduino-serial-controller";
$log_file = "/var/log/arduino-serial-controller/arduino_controller.log";

try {
    if (!isset($_GET['action'])) {
        echo json_encode(['success' => false, 'message' => 'No action specified']);
        exit;
    }

    $action = $_GET['action'];

    switch ($action) {
        case 'service':
            if (!isset($_GET['cmd'])) {
                echo json_encode(['success' => false, 'message' => 'No service command specified']);
                break;
            }

            $cmd = $_GET['cmd'];
            if (!in_array($cmd, ['start', 'stop', 'restart', 'status'])) {
                echo json_encode(['success' => false, 'message' => 'Invalid command: ' . $cmd]);
                break;
            }

            $result = shell_exec("/etc/rc.d/rc.arduino-serial-controller $cmd 2>&1");
            echo json_encode([
                'success' => true, 
                'message' => trim($result), 
                'action' => $cmd
            ]);
            break;

        case 'logs':
            $log_entries = [];
            if (file_exists($log_file)) {
                $lines = file($log_file, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
                if ($lines !== false) {
                    $log_entries = array_slice($lines, -20);
                }
            }
            echo json_encode(['success' => true, 'logs' => $log_entries]);
            break;

        default:
            echo json_encode(['success' => false, 'message' => 'Unknown action: ' . $action]);
            break;
    }

} catch (Exception $e) {
    echo json_encode(['success' => false, 'message' => 'Exception: ' . $e->getMessage()]);
}
?>