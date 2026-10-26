#!/usr/bin/env node
// Local stand-in for Vercel Functions, so the gateway can be exercised before any
// account or domain exists. Routing mirrors Vercel: the path maps to a file under
// api/, and vercel.json's single rewrite moves the tile segments into the query.
//
//   cp .env.example .env && npm run dev
//   MOTO_PROVIDER=fixture npm run dev
import { createServer } from "node:http";

const TILE = /^\/api\/v1\/map\/tiles\/(\d+)\/(\d+)\/(\d+)$/;
const SAFE = /^[A-Za-z0-9/_-]+$/;
const port = Number(process.env.PORT ?? 3000);
const host = process.env.HOST ?? "127.0.0.1";

async function functionFor(pathname) {
  if (!pathname.startsWith("/api/")) return null;
  const name = pathname.slice("/api/".length).replace(/\/$/, "");
  if (name === "" || !SAFE.test(name)) return null;
  try {
    const module = await import(new URL(`../api/${name}.js`, import.meta.url));
    return typeof module.default === "function" ? module.default : null;
  } catch (error) {
    if (error?.code === "ERR_MODULE_NOT_FOUND") return null;
    throw error;
  }
}

const server = createServer(async (request, response) => {
  try {
    const url = new URL(request.url ?? "/", "http://localhost");
    const tile = TILE.exec(url.pathname);
    if (tile) request.url = `/api/v1/map/tiles?z=${tile[1]}&x=${tile[2]}&y=${tile[3]}`;

    const target = tile ? new URL(request.url, "http://localhost") : url;
    const handler = await functionFor(target.pathname);
    if (!handler) {
      response.statusCode = 404;
      response.setHeader("Content-Type", "application/json; charset=utf-8");
      response.end(JSON.stringify({ error: "no local function for this path" }));
      return;
    }
    await handler(request, response);
  } catch (error) {
    response.statusCode = 500;
    response.setHeader("Content-Type", "application/json; charset=utf-8");
    response.end(JSON.stringify({ error: `${error?.name}: ${error?.message}` }));
  }
});

server.listen(port, host, () => {
  console.log(`MOTO GPS gateway (Vercel function shape) on http://${host}:${port}/api/healthz`);
});

for (const signal of ["SIGINT", "SIGTERM"]) {
  process.once(signal, () => server.close(() => process.exit(0)));
}
