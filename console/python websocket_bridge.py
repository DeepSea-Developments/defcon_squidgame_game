import asyncio
import json
import serial
import serial.tools.list_ports
import websockets

# Configuration
WEBSOCKET_HOST = "localhost"
WEBSOCKET_PORT = 8765
SERIAL_BAUDRATE = 115200
MAX_PLAYERS = 4

# Global state
connected_clients = set()
players = {}  # Maps port address to player info { 'player_num': int, 'serial_conn': Serial }
assigned_player_nums = set()

def get_available_player_num():
    """Finds the next available player number."""
    for i in range(1, MAX_PLAYERS + 1):
        if i not in assigned_player_nums:
            return i
    return None

async def handle_websocket_message(message):
    """Parses a message from a websocket client and forwards it to the correct serial device(s)."""
    print(f"Received message from client: {message}")
    try:
        data = json.loads(message)
        
        # If the message targets a specific player, send only to them.
        if "player_id" in data:
            target_player_id = data.get("player_id")
            found_player = False
            for port, player_info in players.items():
                if player_info['player_num'] == target_player_id:
                    try:
                        ser = player_info['serial_conn']
                        if ser.is_open:
                            # Send the original JSON message string, followed by a newline
                            ser.write(message.encode('utf-8') + b'\n')
                            print(f"Forwarded message to Player {player_info['player_num']} on {port}")
                            found_player = True
                            break # Exit loop once player is found
                    except Exception as e:
                        print(f"Error writing to Player {player_info['player_num']} on {port}: {e}")
            if not found_player:
                print(f"Target Player ID {target_player_id} not found or not connected.")

        # Otherwise, broadcast the message to all connected players.
        else:
            print("Broadcasting message to all players.")
            for port, player_info in players.items():
                 try:
                    ser = player_info['serial_conn']
                    if ser.is_open:
                        ser.write(message.encode('utf-8') + b'\n')
                        print(f"Broadcasted message to Player {player_info['player_num']} on {port}")
                 except Exception as e:
                    print(f"Error broadcasting to Player {player_info['player_num']} on {port}: {e}")

    except json.JSONDecodeError:
        print(f"Received non-JSON message from client, ignoring: {message}")
    except Exception as e:
        print(f"Error handling websocket message: {e}")

async def register_client(websocket):
    """Registers a new websocket client and handles its messages."""
    connected_clients.add(websocket)
    print(f"New client connected: {websocket.remote_address}")
    try:
        # Listen for incoming messages from this client
        async for message in websocket:
            await handle_websocket_message(message)
    except websockets.exceptions.ConnectionClosedError:
        print(f"Client connection closed normally: {websocket.remote_address}")
    finally:
        print(f"Client disconnected: {websocket.remote_address}")
        connected_clients.remove(websocket)

async def broadcast(message):
    """Broadcasts a message to all connected websocket clients."""
    if connected_clients:
        await asyncio.wait([client.send(message) for client in connected_clients])

async def read_from_serial(port_address):
    """Reads data from a serial port and broadcasts it."""
    player_info = players.get(port_address)
    if not player_info:
        return

    ser = player_info['serial_conn']
    player_num = player_info['player_num']

    print(f"Started reading from Player {player_num} on {port_address}")

    while ser.is_open:
        try:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()
                if line:
                    #print(f"Player {player_num} ({port_address}): {line}")
                    message = json.dumps({"player": player_num, "data": line})
                    await broadcast(message)
            await asyncio.sleep(0.01)
        except serial.SerialException:
            print(f"Serial error with Player {player_num} on {port_address}. Disconnecting.")
            break
        except Exception as e:
            print(f"An error occurred with Player {player_num} on {port_address}: {e}")
            break

    # Cleanup after disconnection
    print(f"Player {player_num} on {port_address} has disconnected.")
    if port_address in players:
        assigned_player_nums.remove(players[port_address]['player_num'])
        del players[port_address]
        await broadcast(json.dumps({"status": "disconnected", "player": player_num, "port": port_address}))


async def com_port_scanner():
    """Scans for COM ports and manages connections."""
    print("Starting COM port scanner...")
    while True:
        available_ports = serial.tools.list_ports.comports()
        
        # Print detailed information for each available port for debugging
        print("\n--- Scanning for COM ports ---")
        if not available_ports:
            print("No COM ports found.")
        else:
            for port in available_ports:
                print(f"  - Port: {port.device}, Manufacturer: {port.manufacturer}, Description: {port.description}")
        print("----------------------------\n")

        # Filter ports based on description only
        esp_ports = {p.device for p in available_ports if "Serial" in (p.description or "")}
        
        # Check for new connections
        for port in esp_ports:
            if port not in players:
                player_num = get_available_player_num()
                if player_num:
                    try:
                        ser = serial.Serial(port, SERIAL_BAUDRATE, timeout=1)
                        players[port] = {'player_num': player_num, 'serial_conn': ser}
                        assigned_player_nums.add(player_num)
                        print(f"Connected to {port} and assigned Player {player_num}")
                        await broadcast(json.dumps({"status": "connected", "player": player_num, "port": port}))
                        asyncio.create_task(read_from_serial(port))
                    except serial.SerialException as e:
                        print(f"Failed to connect to {port}: {e}")
                else:
                    print(f"Found ESP device on {port}, but no player slots available.")

        # Check for disconnections
        connected_ports = set(players.keys())
        disconnected_ports = connected_ports - esp_ports
        for port in disconnected_ports:
            print(f"Detected disconnection on {port}.")
            player_info = players.get(port)
            if player_info:
                player_info['serial_conn'].close()
                # The read_from_serial task will handle the rest of the cleanup.
        
        await asyncio.sleep(2) # Scan every 2 seconds

async def main():
    """Main function to start the server and scanner."""
    scanner_task = asyncio.create_task(com_port_scanner())
    
    async with websockets.serve(register_client, WEBSOCKET_HOST, WEBSOCKET_PORT):
        print(f"WebSocket server started on ws://{WEBSOCKET_HOST}:{WEBSOCKET_PORT}")
        await asyncio.Future()  # Run forever

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Server is shutting down.")
