#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <iostream>
#include <set>
#include <map>
#include <random>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>

using namespace websocketpp;
using json = nlohmann::json;

typedef server<config::asio> Server;

// ============================================================
//  Minecraft 方塊定義
// ============================================================
struct Block {
    int id;
    std::string name;
    std::string color;
    bool solid;
};

std::map<int, Block> BLOCKS = {
    {0, {0, "空氣", "#000000", false}},
    {1, {1, "草地", "#4a8a3a", true}},
    {2, {2, "泥土", "#6a5a3a", true}},
    {3, {3, "石頭", "#8a8a8a", true}},
    {4, {4, "木頭", "#6a4a2a", true}},
    {5, {5, "木材", "#ca9a6a", true}},
    {6, {6, "鵝卵石", "#7a7a7a", true}},
    {7, {7, "黑曜石", "#222244", true}},
    {8, {8, "砂岩", "#d4c48a", true}},
    {9, {9, "紅磚", "#aa5a3a", true}},
    {10, {10, "玻璃", "#88ccff", false}},
    {11, {11, "雪", "#ffffff", true}},
};

// ============================================================
//  世界 (Chunk 系統)
// ============================================================
struct Chunk {
    int x, z;
    std::map<int, int> blocks; // position -> blockId
    bool loaded = false;
};

class World {
public:
    void loadChunk(int cx, int cz) {
        Chunk chunk;
        chunk.x = cx;
        chunk.z = cz;
        
        // 用隨機生成地形 (簡單版)
        std::mt19937 rng(cx * 10000 + cz);
        std::uniform_int_distribution<int> dist(0, 5);
        
        for (int bx = 0; bx < 16; bx++) {
            for (int bz = 0; bz < 16; bz++) {
                int height = 2 + (cx + cz + bx + bz) % 4;
                for (int by = 0; by < height; by++) {
                    int blockId = 2; // 泥土
                    if (by == height - 1) blockId = 1; // 草地
                    if (by < 1) blockId = 3; // 石頭
                    chunk.blocks[bx * 256 + bz * 16 + by] = blockId;
                }
            }
        }
        
        m_chunks[{cx, cz}] = chunk;
    }
    
    int getBlock(int cx, int cz, int bx, int by, int bz) {
        auto it = m_chunks.find({cx, cz});
        if (it == m_chunks.end()) return 0;
        int pos = bx * 256 + bz * 16 + by;
        auto it2 = it->second.blocks.find(pos);
        if (it2 == it->second.blocks.end()) return 0;
        return it2->second;
    }
    
    void setBlock(int cx, int cz, int bx, int by, int bz, int blockId) {
        auto it = m_chunks.find({cx, cz});
        if (it == m_chunks.end()) return;
        int pos = bx * 256 + bz * 16 + by;
        it->second.blocks[pos] = blockId;
    }

private:
    std::map<std::pair<int,int>, Chunk> m_chunks;
};

// ============================================================
//  玩家資料
// ============================================================
struct Player {
    double x = 0, y = 1.8, z = 0;
    double rotX = 0, rotY = 0;
    int health = 20;
    int kills = 0;
    std::string name = "玩家";
    std::map<int, int> inventory; // blockId -> count
    int selectedSlot = 0;
};

// ============================================================
//  合成配方
// ============================================================
struct Recipe {
    std::string name;
    std::map<int, int> ingredients;
    int result;
    int resultCount;
};

std::vector<Recipe> RECIPES = {
    {"木材", {{4, 1}}, 5, 4},        // 1 木頭 → 4 木材
    {"木板", {{5, 4}}, 5, 1},        // 4 木板 → 1 木板 (佔位)
    {"鵝卵石", {{2, 4}}, 6, 4},      // 4 泥土 → 4 鵝卵石
};

// ============================================================
//  Minecraft 伺服器主程式
// ============================================================
class MinecraftServer {
public:
    MinecraftServer() {
        m_server.init_asio();
        
        m_server.set_open_handler([this](auto hdl) {
            onOpen(hdl);
        });
        
        m_server.set_close_handler([this](auto hdl) {
            onClose(hdl);
        });
        
        m_server.set_message_handler([this](auto hdl, auto msg) {
            onMessage(hdl, msg);
        });
        
        // 載入初始 Chunk
        for (int x = -3; x <= 3; x++) {
            for (int z = -3; z <= 3; z++) {
                m_world.loadChunk(x, z);
            }
        }
    }
    
    void run(int port) {
        m_server.listen(port);
        m_server.start_accept();
        std::cout << "⛏️ Minecraft 伺服器啟動在 port " << port << std::endl;
        std::cout << "🌐 連線: ws://localhost:" << port << std::endl;
        m_server.run();
    }

private:
    void onOpen(connection_hdl hdl) {
        int id = m_nextPlayerId++;
        m_players[hdl] = Player();
        m_playerIds[hdl] = id;
        m_playerConnections[id] = hdl;
        
        std::cout << "🟢 玩家 " << id << " 加入了世界" << std::endl;
        
        // 發送初始化資料
        sendTo(hdl, {
            {"type", "init"},
            {"id", id},
            {"world", getWorldData()},
            {"players", getPlayersInfo()}
        });
        
        broadcast({
            {"type", "player_join"},
            {"id", id},
            {"name", "玩家" + std::to_string(id)},
            {"x", 0.0},
            {"z", 0.0}
        });
    }
    
    void onClose(connection_hdl hdl) {
        auto it = m_playerIds.find(hdl);
        if (it != m_playerIds.end()) {
            int id = it->second;
            std::cout << "🔴 玩家 " << id << " 離開了世界" << std::endl;
            
            m_players.erase(hdl);
            m_playerIds.erase(hdl);
            m_playerConnections.erase(id);
            
            broadcast({
                {"type", "player_leave"},
                {"id", id}
            });
        }
    }
    
    void onMessage(connection_hdl hdl, Server::message_ptr msg) {
        try {
            auto data = json::parse(msg->get_payload());
            std::string type = data["type"];
            
            if (type == "move") {
                auto& p = m_players[hdl];
                p.x = data["x"];
                p.z = data["z"];
                p.rotX = data["rotX"];
                p.rotY = data["rotY"];
                
                broadcast({
                    {"type", "update"},
                    {"id", m_playerIds[hdl]},
                    {"x", p.x},
                    {"z", p.z},
                    {"rotX", p.rotX},
                    {"rotY", p.rotY}
                });
            }
            else if (type == "place_block") {
                int cx = data["cx"];
                int cz = data["cz"];
                int bx = data["bx"];
                int by = data["by"];
                int bz = data["bz"];
                int blockId = data["blockId"];
                
                m_world.setBlock(cx, cz, bx, by, bz, blockId);
                
                broadcast({
                    {"type", "block_placed"},
                    {"cx", cx},
                    {"cz", cz},
                    {"bx", bx},
                    {"by", by},
                    {"bz", bz},
                    {"blockId", blockId}
                });
            }
            else if (type == "break_block") {
                int cx = data["cx"];
                int cz = data["cz"];
                int bx = data["bx"];
                int by = data["by"];
                int bz = data["bz"];
                
                int blockId = m_world.getBlock(cx, cz, bx, by, bz);
                m_world.setBlock(cx, cz, bx, by, bz, 0);
                
                // 掉落物 (加入背包)
                m_players[hdl].inventory[blockId]++;
                
                broadcast({
                    {"type", "block_broken"},
                    {"cx", cx},
                    {"cz", cz},
                    {"bx", bx},
                    {"by", by},
                    {"bz", bz},
                    {"playerId", m_playerIds[hdl]}
                });
            }
            else if (type == "craft") {
                int recipeId = data["recipeId"];
                if (recipeId < RECIPES.size()) {
                    Recipe& r = RECIPES[recipeId];
                    auto& inv = m_players[hdl].inventory;
                    
                    // 檢查材料
                    bool hasAll = true;
                    for (auto& ing : r.ingredients) {
                        if (inv[ing.first] < ing.second) {
                            hasAll = false;
                            break;
                        }
                    }
                    
                    if (hasAll) {
                        // 消耗材料
                        for (auto& ing : r.ingredients) {
                            inv[ing.first] -= ing.second;
                            if (inv[ing.first] <= 0) inv.erase(ing.first);
                        }
                        // 給予結果
                        inv[r.result] += r.resultCount;
                        
                        sendTo(hdl, {
                            {"type", "craft_success"},
                            {"result", r.result},
                            {"count", r.resultCount},
                            {"inventory", inv}
                        });
                    } else {
                        sendTo(hdl, {
                            {"type", "craft_fail"},
                            {"message", "材料不足!"}
                        });
                    }
                }
            }
            else if (type == "chat") {
                broadcast({
                    {"type", "chat"},
                    {"id", m_playerIds[hdl]},
                    {"name", m_players[hdl].name},
                    {"msg", data["msg"]}
                });
            }
            
        } catch (const std::exception& e) {
            std::cerr << "❌ 錯誤: " << e.what() << std::endl;
        }
    }
    
    json getWorldData() {
        json result = json::array();
        for (int cx = -3; cx <= 3; cx++) {
            for (int cz = -3; cz <= 3; cz++) {
                for (int bx = 0; bx < 16; bx++) {
                    for (int bz = 0; bz < 16; bz++) {
                        for (int by = 0; by < 8; by++) {
                            int blockId = m_world.getBlock(cx, cz, bx, by, bz);
                            if (blockId > 0) {
                                result.push_back({
                                    {"cx", cx},
                                    {"cz", cz},
                                    {"bx", bx},
                                    {"by", by},
                                    {"bz", bz},
                                    {"id", blockId}
                                });
                            }
                        }
                    }
                }
            }
        }
        return result;
    }
    
    json getPlayersInfo() {
        json result = json::array();
        for (auto& pair : m_players) {
            int id = m_playerIds[pair.first];
            auto& p = pair.second;
            result.push_back({
                {"id", id},
                {"name", p.name},
                {"x", p.x},
                {"z", p.z},
                {"rotX", p.rotX},
                {"rotY", p.rotY},
                {"kills", p.kills}
            });
        }
        return result;
    }
    
    void sendTo(connection_hdl hdl, const json& data) {
        m_server.send(hdl, data.dump(), websocketpp::frame::opcode::text);
    }
    
    void broadcast(const json& data) {
        std::string msg = data.dump();
        for (auto hdl : m_connections) {
            m_server.send(hdl, msg, websocketpp::frame::opcode::text);
        }
    }
    
    Server m_server;
    World m_world;
    std::set<connection_hdl, std::owner_less<connection_hdl>> m_connections;
    std::map<connection_hdl, Player, std::owner_less<connection_hdl>> m_players;
    std::map<connection_hdl, int, std::owner_less<connection_hdl>> m_playerIds;
    std::map<int, connection_hdl> m_playerConnections;
    int m_nextPlayerId = 1;
};

// ============================================================
//  主程式
// ============================================================
int main() {
    try {
        MinecraftServer server;
        server.run(8080);
    } catch (const std::exception& e) {
        std::cerr << "❌ 伺服器錯誤: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}