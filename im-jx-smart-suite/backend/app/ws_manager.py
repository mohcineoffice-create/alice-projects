import json
import asyncio
import logging
from fastapi import WebSocket

logger = logging.getLogger("imjx")

HEARTBEAT_INTERVAL_SECONDS = 30


class ConnectionManager:
    def __init__(self):
        # each entry: {"ws": WebSocket, "role": str, "department": str, "username": str}
        self.connections: list[dict] = []

    async def connect(self, websocket: WebSocket, user: dict):
        await websocket.accept()
        self.connections.append({
            "ws": websocket,
            "role": user.get("role"),
            "department": user.get("department"),
            "username": user.get("sub"),
        })

    def disconnect(self, websocket: WebSocket):
        self.connections = [c for c in self.connections if c["ws"] is not websocket]

    async def send_to(self, conn: dict, data: dict):
        try:
            await conn["ws"].send_text(json.dumps(data))
        except Exception:
            # La connexion est morte (client déconnecté brutalement, réseau coupé...) :
            # on la retire immédiatement de la liste. Sans ça, une connexion en échec
            # restait indéfiniment dans self.connections et échouait à chaque broadcast
            # suivant -> dégradation progressive des performances (fuite mémoire).
            logger.info("Connexion WebSocket morte détectée (user=%s), retrait de la liste", conn.get("username"))
            self.disconnect(conn["ws"])

    async def broadcast_to_admins(self, data: dict):
        for c in list(self.connections):
            if c["role"] == "admin":
                await self.send_to(c, data)

    async def broadcast_command(self, data: dict, target_department: str, target_role: str):
        for c in list(self.connections):
            dep_match = target_department == "ALL" or c["department"] == target_department
            role_match = target_role == "ALL" or c["role"] == target_role
            if dep_match and role_match and c["role"] != "admin":
                await self.send_to(c, data)

    async def broadcast_all(self, data: dict):
        for c in list(self.connections):
            await self.send_to(c, data)

    def get_online_usernames(self) -> list[str]:
        seen = set()
        for c in self.connections:
            if c["username"]:
                seen.add(c["username"])
        return sorted(seen)

    def get_connection_count_by_department(self) -> dict[str, int]:
        counts: dict[str, int] = {}
        for c in self.connections:
            dep = c.get("department") or "?"
            counts[dep] = counts.get(dep, 0) + 1
        return counts

    async def broadcast_scoped(self, data: dict, scope: str):
        """Diffuse à tous les utilisateurs autorisés à voir ce scope (channel de chat
        OU département machine) : GENERAL -> tout le monde ; sinon -> même département + admins."""
        for c in list(self.connections):
            if scope == "GENERAL" or c["department"] == scope or c["role"] == "admin":
                await self.send_to(c, data)

    async def broadcast_presence(self, username: str, status: str):
        await self.broadcast_all({"type": "presence", "username": username, "status": status})

    async def heartbeat_loop(self):
        """Ping périodique de toutes les connexions actives : permet de détecter et de
        nettoyer les clients déconnectés silencieusement (veille, perte réseau brutale)
        sans attendre le prochain message applicatif."""
        while True:
            await asyncio.sleep(HEARTBEAT_INTERVAL_SECONDS)
            for c in list(self.connections):
                await self.send_to(c, {"type": "ping"})


manager = ConnectionManager()
