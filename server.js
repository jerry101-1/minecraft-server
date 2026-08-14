const WebSocket = require('ws');

// 使用 Render 給的 PORT
const port = process.env.PORT || 8080;

const wss = new WebSocket.Server({ port });

let players = {};
let nextId = 1;

console.log('⛏️ Minecraft 伺服器啟動在 port ' + port);

wss.on('connection', (ws) => {
    const id = nextId++;
    players[id] = {
        x: 0, y: 1.8, z: 0,
        rotX: 0, rotY: 0,
        name: '玩家' + id
    };

    console.log('🟢 玩家 ' + id + ' 加入了世界');

    // 發送初始化資料
    ws.send(JSON.stringify({
        type: 'init',
        id: id,
        players: Object.keys(players).map(k => ({
            id: parseInt(k),
            x: players[k].x,
            z: players[k].z,
            name: players[k].name
        }))
    }));

    // 廣播新玩家
    broadcast({
        type: 'player_join',
        id: id,
        name: players[id].name,
        x: players[id].x,
        z: players[id].z
    });

    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);

            if (data.type === 'move') {
                players[id].x = data.x;
                players[id].z = data.z;
                players[id].rotX = data.rotX;
                players[id].rotY = data.rotY;

                broadcast({
                    type: 'update',
                    id: id,
                    x: players[id].x,
                    z: players[id].z,
                    rotX: players[id].rotX,
                    rotY: players[id].rotY
                });
            }
            else if (data.type === 'chat') {
                broadcast({
                    type: 'chat',
                    id: id,
                    name: players[id].name,
                    msg: data.msg
                });
            }
        } catch (e) {
            console.error('錯誤:', e);
        }
    });

    ws.on('close', () => {
        console.log('🔴 玩家 ' + id + ' 離開了世界');
        delete players[id];
        broadcast({ type: 'player_leave', id: id });
    });
});

function broadcast(data) {
    const msg = JSON.stringify(data);
    wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(msg);
        }
    });
}
