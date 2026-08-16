const express = require('express');
const WebSocket = require('ws');
const path = require('path');

const app = express();
const port = process.env.PORT || 8080;

app.use(express.static(path.join(__dirname, 'public')));

app.get('*', (req, res) => {
    res.sendFile(path.join(__dirname, 'public', 'index.html'));
});

const server = app.listen(port, () => {
    console.log('⛏️ Minecraft 伺服器啟動在 port ' + port);
});

const wss = new WebSocket.Server({ server });

// ============================================================
//  方塊定義
// ============================================================
const BLOCKS = {
    0: { id: 0, name: '空氣', color: '#000000', solid: false },
    1: { id: 1, name: '草地', color: '#4a8a3a', solid: true },
    2: { id: 2, name: '泥土', color: '#6a5a3a', solid: true },
    3: { id: 3, name: '石頭', color: '#8a8a8a', solid: true },
    4: { id: 4, name: '木頭', color: '#6a4a2a', solid: true },
    5: { id: 5, name: '木材', color: '#ca9a6a', solid: true },
    6: { id: 6, name: '樹葉', color: '#3a8a3a', solid: true },
    7: { id: 7, name: '沙子', color: '#d4c48a', solid: true },
    8: { id: 8, name: '鵝卵石', color: '#7a7a7a', solid: true },
    9: { id: 9, name: '紅磚', color: '#aa5a3a', solid: true },
    10: { id: 10, name: '玻璃', color: '#88ccff', solid: false },
};

// ============================================================
//  世界儲存
// ============================================================
let world = {};

function getBlockKey(x, y, z) {
    return `${Math.round(x)},${Math.round(y)},${Math.round(z)}`;
}

function setBlock(x, y, z, blockId) {
    const key = getBlockKey(x, y, z);
    if (blockId === 0) {
        delete world[key];
    } else {
        world[key] = blockId;
    }
}

function getBlock(x, y, z) {
    const key = getBlockKey(x, y, z);
    return world[key] || 0;
}

// ============================================================
//  世界生成 (Minecraft 風格地形)
// ============================================================
function generateWorld() {
    const size = 20;
    for (let x = -size; x < size; x++) {
        for (let z = -size; z < size; z++) {
            // 地形高度 (使用簡單雜訊)
            let height = 4;
            height += Math.floor(Math.sin(x * 0.2) * 2);
            height += Math.floor(Math.cos(z * 0.15) * 2);
            height += Math.floor(Math.sin(x * 0.05 + z * 0.08) * 3);
            height = Math.max(1, Math.min(8, height));
            
            for (let y = 0; y < height; y++) {
                let blockId = 2; // 泥土
                if (y === height - 1) blockId = 1; // 草地
                if (y === 0) blockId = 3; // 石頭
                if (y < 0) blockId = 3; // 石頭
                setBlock(x, y, z, blockId);
            }
        }
    }
    
    // 樹木
    for (let i = 0; i < 15; i++) {
        const x = Math.floor((Math.random() - 0.5) * 30);
        const z = Math.floor((Math.random() - 0.5) * 30);
        if (Math.abs(x) < 4 && Math.abs(z) < 4) continue;
        generateTree(x, z);
    }
    
    // 沙子 (靠近水)
    for (let i = 0; i < 10; i++) {
        const x = Math.floor((Math.random() - 0.5) * 30);
        const z = Math.floor((Math.random() - 0.5) * 30);
        for (let y = 0; y < 3; y++) {
            if (getBlock(x, y, z) === 0) {
                setBlock(x, y, z, 7); // 沙子
                break;
            }
        }
    }
    
    console.log('🌍 世界已生成，方塊數:', Object.keys(world).length);
}

function generateTree(x, z) {
    let groundY = 0;
    for (let y = 10; y >= -5; y--) {
        if (getBlock(x, y, z) !== 0) {
            groundY = y;
            break;
        }
    }
    
    const trunkHeight = 4 + Math.floor(Math.random() * 2);
    for (let i = 1; i <= trunkHeight; i++) {
        setBlock(x, groundY + i, z, 4);
    }
    // 樹葉 (圓形)
    for (let dx = -2; dx <= 2; dx++) {
        for (let dz = -2; dz <= 2; dz++) {
            for (let dy = trunkHeight - 2; dy <= trunkHeight; dy++) {
                const dist = Math.abs(dx) + Math.abs(dz);
                if (dist > 3) continue;
                if (dist === 3 && Math.random() > 0.3) continue;
                if (dx === 0 && dz === 0 && dy === trunkHeight) continue;
                setBlock(x + dx, groundY + dy, z + dz, 6);
            }
        }
    }
}

generateWorld();

// ============================================================
//  玩家管理
// ============================================================
let players = {};
let nextId = 1;

wss.on('connection', (ws) => {
    const id = nextId++;
    players[id] = {
        x: 0, y: 2, z: 0,
        rotX: 0, rotY: 0,
        name: '玩家' + id,
        inventory: {}
    };

    console.log('🟢 玩家 ' + id + ' 加入了世界');

    ws.send(JSON.stringify({
        type: 'init',
        id: id,
        world: world,
        players: getPlayersInfo()
    }));

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
            handleMessage(id, ws, data);
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

function handleMessage(id, ws, data) {
    const p = players[id];
    if (!p) return;

    if (data.type === 'move') {
        p.x = data.x;
        p.y = data.y;
        p.z = data.z;
        p.rotX = data.rotX;
        p.rotY = data.rotY;
        broadcast({
            type: 'update',
            id: id,
            x: p.x,
            y: p.y,
            z: p.z,
            rotX: p.rotX,
            rotY: p.rotY
        });
    }
    else if (data.type === 'break_block') {
        const x = data.x;
        const y = data.y;
        const z = data.z;
        const blockId = getBlock(x, y, z);
        if (blockId === 0) return;
        if (blockId === 3) return; // 石頭不能挖
        
        p.inventory[blockId] = (p.inventory[blockId] || 0) + 1;
        setBlock(x, y, z, 0);
        
        broadcast({
            type: 'block_update',
            x: x,
            y: y,
            z: z,
            blockId: 0
        });
        
        ws.send(JSON.stringify({
            type: 'inventory',
            inventory: p.inventory
        }));
    }
    else if (data.type === 'place_block') {
        const x = data.x;
        const y = data.y;
        const z = data.z;
        const blockId = data.blockId;
        
        if (!p.inventory[blockId] || p.inventory[blockId] <= 0) return;
        if (blockId === 0) return;
        if (getBlock(x, y, z) !== 0) return;
        
        p.inventory[blockId]--;
        setBlock(x, y, z, blockId);
        
        broadcast({
            type: 'block_update',
            x: x,
            y: y,
            z: z,
            blockId: blockId
        });
        
        ws.send(JSON.stringify({
            type: 'inventory',
            inventory: p.inventory
        }));
    }
}

function getPlayersInfo() {
    const result = [];
    for (const id in players) {
        const p = players[id];
        result.push({
            id: parseInt(id),
            name: p.name,
            x: p.x,
            y: p.y,
            z: p.z,
            rotX: p.rotX,
            rotY: p.rotY
        });
    }
    return result;
}

function broadcast(data) {
    const msg = JSON.stringify(data);
    wss.clients.forEach(client => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(msg);
        }
    });
}
