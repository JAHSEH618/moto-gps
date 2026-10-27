import { getVercelGateway } from "./vercel-app.js";

const FUNCTION_PREFIX = "/api";
const TILE_PATH = "/v1/map/tiles";

function stripFunctionPrefix(pathname) {
  if (pathname === FUNCTION_PREFIX) return "/";
  if (pathname.startsWith(`${FUNCTION_PREFIX}/`)) return pathname.slice(FUNCTION_PREFIX.length);
  return pathname;
}

/**
 * Rebuild the URL the gateway expects from whatever Vercel hands the function.
 *
 * The tile route arrives as either `/api/v1/map/tiles/15/1/2` (a rewrite that keeps
 * the path) or `/api/v1/map/tiles?z=15&x=1&y=2` (a rewrite that moves the segments).
 * The gateway rejects any query string on the tile route, so both shapes collapse to
 * the same clean path here.
 */
export function logicalRequestUrl(requestUrl) {
  const url = new URL(requestUrl ?? "/", "http://function.internal");
  let pathname = stripFunctionPrefix(url.pathname);

  // A rewrite may arrive as segments, as query, or as both at once: Vercel routes the
  // destination path but hands the function the original URL plus the destination query.
  // The gateway rejects any query string on the tile route, so normalise to one clean path.
  if (pathname === TILE_PATH || pathname.startsWith(`${TILE_PATH}/`)) {
    const z = url.searchParams.get("z");
    const x = url.searchParams.get("x");
    const y = url.searchParams.get("y");
    if (z !== null && x !== null && y !== null) {
      pathname = `${TILE_PATH}/${encodeURIComponent(z)}/${encodeURIComponent(x)}/${encodeURIComponent(y)}`;
      for (const key of ["z", "x", "y"]) url.searchParams.delete(key);
    }
  }

  const search = url.searchParams.toString();
  return search === "" ? pathname : `${pathname}?${search}`;
}

// Our own messages only; never a stack, an upstream URL or a configured value.
function safeDetail(error) {
  const name = typeof error?.name === "string" ? error.name : "Error";
  const message = typeof error?.message === "string" ? error.message : "";
  return `${name}: ${message}`.slice(0, 200);
}

function sendUnavailable(response, error) {
  const body = JSON.stringify({
    protocol_version: 1,
    request_id: null,
    error: {
      code: "SERVER_MISCONFIGURED",
      message: "gateway is unavailable; check deployment configuration",
      retryable: false,
      detail: safeDetail(error),
    },
  });
  if (!response.headersSent) {
    response.statusCode = 503;
    response.setHeader("Content-Type", "application/json; charset=utf-8");
    response.setHeader("Cache-Control", "no-store");
    response.setHeader("X-Gateway-Failure", "1");
  }
  if (!response.writableEnded) response.end(body);
}

/**
 * Vercel Node function entry point.
 *
 * The request and response are real http.IncomingMessage/http.ServerResponse objects.
 * The gateway handler is awaited, so anything that escapes (a bad environment, an
 * adapter bug, a platform surprise) becomes a visible JSON error instead of the
 * platform's opaque FUNCTION_INVOCATION_FAILED.
 */
export async function handle(request, response) {
  try {
    const { handler } = getVercelGateway();
    request.url = logicalRequestUrl(request.url);
    await handler(request, response);
  } catch (error) {
    console.error("gateway invocation failed:", error?.stack ?? error);
    try {
      sendUnavailable(response, error);
    } catch {
      // Nothing left to say if even the error response cannot be written.
    }
  }
}

export default handle;
