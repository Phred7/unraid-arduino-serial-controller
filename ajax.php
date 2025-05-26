<?php
/* AJAX Handler for Arduino Serial Controller */
header('Content-Type: application/json');

$plugin = "arduino-serial-controller";
$settings_file = "/boot/config/plugins/$plugin/settings.cfg";
$log_file = "/var/log/arduino-serial-controller/arduino_controller.log";

try {
    if (isset($_GET['action'])) {
        $action = $_GET['action'];

        if ($action == 'service' && isset($_GET['cmd'])) {
            $cmd = $_GET['cmd'];
            if (in_array($cmd, ['start', 'stop', 'restart', 'status'])) {
                $result = shell_exec("/etc/rc.d/rc.arduino-serial-controller $cmd 2>&1");
                echo json_encode(['success' => true, 'message' => trim($result), 'action' => $cmd]);
            } else {
                echo json_encode(['success' => false, 'message' => 'Invalid command: ' . $cmd]);
            }
        }
        else if ($action == 'save') {
            // Fixed the condition that was causing hanging
            if (empty($_POST)) {
                echo json_encode(['success' => false, 'message' => 'No POST data received']);
                exit;
            }

            // Ultra-simple save - just write the file
            $serial_port = trim($_POST['serial_port']);
            $baud_rate = trim($_POST['baud_rate']);
            $update_interval = trim($_POST['update_interval']);
            $timeout = trim($_POST['timeout']);
            $log_level = trim($_POST['log_level']);
            $retry_attempts = trim($_POST['retry_attempts']);
            $retry_delay = trim($_POST['retry_delay']);

            // Create config directory
            $config_dir = dirname($settings_file);
            if (!is_dir($config_dir)) {
                mkdir($config_dir, 0755, true);
            }

            // Write config file
            $content = "# Arduino Serial Controller Configuration\n";
            $content .= "# Generated on " . date('Y-m-d H:i:s') . "\n\n";
            $content .= "serial_port=" . $serial_port . "\n";
            $content .= "baud_rate=" . $baud_rate . "\n";
            $content .= "update_interval=" . $update_interval . "\n";
            $content .= "timeout=" . $timeout . "\n";
            $content .= "log_level=" . $log_level . "\n";
            $content .= "retry_attempts=" . $retry_attempts . "\n";
            $content .= "retry_delay=" . $retry_delay . "\n";

            $write_result = file_put_contents($settings_file, $content);

            if ($write_result !== false) {
                echo json_encode([
                    'success' => true,
                    'message' => 'Settings saved! Use the Restart Service button to apply changes.',
                    'bytes_written' => $write_result,
                    'file_path' => $settings_file
                ]);
            } else {
                echo json_encode([
                    'success' => false,
                    'message' => 'Failed to write settings file',
                    'file_path' => $settings_file,
                    'directory_writable' => is_writable($config_dir)
                ]);
            }
        }
        else if ($action == 'logs') {
            $log_entries = [];
            if (file_exists($log_file)) {
                $lines = file($log_file, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);
                if ($lines !== false) {
                    $log_entries = array_slice($lines, -20);
                }
            }
            echo json_encode(['success' => true, 'logs' => $log_entries]);
        }
        else if ($action == 'test') {
            echo json_encode([
                'success' => true,
                'message' => 'AJAX working correctly',
                'timestamp' => date('Y-m-d H:i:s'),
                'settings_file' => $settings_file,
                'settings_dir_exists' => is_dir(dirname($settings_file)),
                'settings_dir_writable' => is_writable(dirname($settings_file))
            ]);
        }
        else if ($action == 'debug_save') {
            // Debug endpoint to test just the file write
            $test_content = "# Test file write at " . date('Y-m-d H:i:s') . "\n";
            $config_dir = dirname($settings_file);

            if (!is_dir($config_dir)) {
                mkdir($config_dir, 0755, true);
            }

            $result = file_put_contents($settings_file, $test_content);

            echo json_encode([
                'success' => $result !== false,
                'message' => $result !== false ? 'Test file write successful' : 'Test file write failed',
                'bytes_written' => $result,
                'file_path' => $settings_file,
                'directory_exists' => is_dir($config_dir),
                'directory_writable' => is_writable($config_dir),
                'file_exists' => file_exists($settings_file),
                'file_writable' => file_exists($settings_file) ? is_writable($settings_file) : 'file does not exist'
            ]);
        }
        else {
            echo json_encode(['success' => false, 'message' => 'Unknown action: ' . $action]);
        }
    } else {
        echo json_encode(['success' => false, 'message' => 'No action specified']);
    }
} catch (Exception $e) {
    echo json_encode(['success' => false, 'message' => 'Exception: ' . $e->getMessage()]);
}
?>