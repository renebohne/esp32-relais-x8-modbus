import Modbus from 'jsmodbus';
import net from 'net';
import readline from 'readline';

// --- Configuration ---
const HOST = '192.168.20.119'; // <-- IMPORTANT: Change to your ESP32's IP address
const PORT = 502;
const UNIT_ID = 1;
const TIMEOUT = 5000; // 5 seconds

// Modbus Address Constants (matching the ESP32 code)
const COIL_MANUAL_START_ADDR = 0;
const HREG_DURATION_START_ADDR = 100;
const COIL_ARM_RELAY_START_ADDR = 20;
const COIL_GLOBAL_TRIGGER_ADDR = 30;
const COIL_ANY_RELAY_ON_ADDR = 40;
const RELAY_COUNT = 8;

// Device Information Registers
const HREG_FIRMWARE_VERSION_ADDR = 500;
const HREG_DEVICE_NAME_START_ADDR = 501;
const HREG_DEVICE_NAME_LEN = 10;
const HREG_SERIAL_NUMBER_START_ADDR = 511; // Uses 2 registers


// --- Setup ---
const socket = new net.Socket();
const client = new Modbus.client.TCP(socket, UNIT_ID);

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

const question = (query) => new Promise(resolve => rl.question(query, resolve));

// --- Core Functions ---

async function readAllRelayStatus() {
    console.log('\nReading current relay status...');
    try {
        const response = await client.readCoils(COIL_MANUAL_START_ADDR, RELAY_COUNT);
        const anyOnResponse = await client.readCoils(COIL_ANY_RELAY_ON_ADDR, 1);
        
        console.log('---------------------------------');
        response.response.body.valuesAsArray.forEach((status, index) => {
            console.log(`Relay ${index}: ${status === 1 ? 'ON' : 'OFF'}`);
        });
        console.log('---');
        console.log(`Master Status (Any Relay On): ${anyOnResponse.response.body.valuesAsArray[0] === 1 ? 'ON' : 'OFF'}`);
        console.log('---------------------------------');

    } catch (err) {
        console.error('Error reading coils:', err.message);
    }
}

async function toggleRelayManual() {
    try {
        const relayNumStr = await question('Enter relay number (0-7): ');
        const relayNum = parseInt(relayNumStr, 10);
        if (isNaN(relayNum) || relayNum < 0 || relayNum > 7) {
            console.log('Invalid relay number.');
            return;
        }

        const stateStr = await question('Enter state (1 for ON, 0 for OFF): ');
        const state = parseInt(stateStr, 10);
        if (state !== 0 && state !== 1) {
            console.log('Invalid state.');
            return;
        }

        console.log(`\nSetting Relay ${relayNum} to ${state === 1 ? 'ON' : 'OFF'}...`);
        await client.writeSingleCoil(COIL_MANUAL_START_ADDR + relayNum, state === 1);
        console.log('Command sent successfully.');

    } catch (err) {
        console.error('Error writing coil:', err.message);
    }
}

async function configureAndArmRelay() {
    try {
        const relayNumStr = await question('Enter relay number to arm (0-7): ');
        const relayNum = parseInt(relayNumStr, 10);
        if (isNaN(relayNum) || relayNum < 0 || relayNum > 7) {
            console.log('Invalid relay number.');
            return;
        }

        const durationStr = await question('Enter duration in milliseconds for this relay: ');
        const duration = parseInt(durationStr, 10);
        if (isNaN(duration) || duration <= 0) {
            console.log('Invalid duration.');
            return;
        }

        console.log(`\nSetting duration for Relay ${relayNum} to ${duration}ms...`);
        await client.writeSingleRegister(HREG_DURATION_START_ADDR + relayNum, duration);

        console.log(`Arming Relay ${relayNum}...`);
        await client.writeSingleCoil(COIL_ARM_RELAY_START_ADDR + relayNum, true);
        
        console.log(`Relay ${relayNum} is ARMED and ready to be triggered.`);

    } catch (err) {
        console.error('Error during arming sequence:', err.message);
    }
}

async function executeGlobalTrigger() {
    console.log('\nSending GLOBAL TRIGGER to start all armed relays...');
    try {
        await client.writeSingleCoil(COIL_GLOBAL_TRIGGER_ADDR, true);
        console.log('Global trigger command sent successfully.');
    } catch (err) {
        console.error('Error sending global trigger:', err.message);
    }
}

async function readDeviceInfo() {
    console.log('\nReading Device Information...');
    try {
        // 1. Read Firmware Version
        const fwVersionResp = await client.readHoldingRegisters(HREG_FIRMWARE_VERSION_ADDR, 1);
        const fwVersion = fwVersionResp.response.body.values[0];
        const major = Math.floor(fwVersion / 100);
        const minor = Math.floor((fwVersion % 100) / 10);
        const patch = fwVersion % 10;

        // 2. Read Device Name
        const deviceNameResp = await client.readHoldingRegisters(HREG_DEVICE_NAME_START_ADDR, HREG_DEVICE_NAME_LEN);
        let deviceName = '';
        deviceNameResp.response.body.values.forEach(reg => {
            const char1 = (reg >> 8) & 0xFF;
            const char2 = reg & 0xFF;
            if (char1 !== 0) deviceName += String.fromCharCode(char1);
            if (char2 !== 0) deviceName += String.fromCharCode(char2);
        });

        // 3. *** UPDATED: Read 7-digit Serial Number from two registers ***
        const serialNumResp = await client.readHoldingRegisters(HREG_SERIAL_NUMBER_START_ADDR, 2);
        const highWord = serialNumResp.response.body.values[0];
        const lowWord = serialNumResp.response.body.values[1];
        const serialNumber = (highWord << 16) | lowWord;

        console.log('---------------------------------');
        console.log(`Device Name:      ${deviceName}`);
        console.log(`Firmware Version: v${major}.${minor}.${patch}`);
        console.log(`Serial Number:    ${serialNumber}`);
        console.log('---------------------------------');

    } catch (err) {
        console.error('Error reading device info:', err.message);
    }
}


// --- Main Application Logic ---

function showMenu() {
    console.log(`\n--- Modbus Relay Tester ---`);
    console.log(`Connected to: ${HOST}:${PORT}`);
    console.log('1. Read all relay statuses');
    console.log('2. Toggle a relay manually (ON/OFF)');
    console.log('3. Configure and Arm a Relay for Timed Run');
    console.log('4. Execute Global Trigger (Start all armed relays)');
    console.log('5. Read Device Information');
    console.log('6. Exit');
}

async function main() {
    while (true) {
        showMenu();
        const choice = await question('Enter your choice: ');

        switch (choice.trim()) {
            case '1':
                await readAllRelayStatus();
                break;
            case '2':
                await toggleRelayManual();
                break;
            case '3':
                await configureAndArmRelay();
                break;
            case '4':
                await executeGlobalTrigger();
                break;
            case '5':
                await readDeviceInfo();
                break;
            case '6':
                console.log('Exiting...');
                socket.end();
                rl.close();
                return;
            default:
                console.log('Invalid choice. Please try again.');
        }
    }
}

// --- Connection Handling ---
socket.on('connect', async () => {
    console.log('Successfully connected to Modbus server.');
    main(); // Start the main application loop
});

socket.on('error', (err) => {
    console.error('Connection Error:', err.message);
    rl.close();
});

socket.on('close', () => {
    console.log('Connection closed.');
    rl.close();
});

console.log(`Attempting to connect to ${HOST}:${PORT}...`);
socket.connect({ host: HOST, port: PORT });
