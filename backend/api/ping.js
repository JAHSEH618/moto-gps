// Deliberately imports nothing: if this fails, the problem is the platform or the
// module format, not the gateway.
export default function ping(request, response) {
  const body = JSON.stringify({
    ok: true,
    node: process.version,
    region: process.env.VERCEL_REGION ?? null,
    method: request.method,
    url: request.url,
  });
  response.statusCode = 200;
  response.setHeader("Content-Type", "application/json; charset=utf-8");
  response.setHeader("Cache-Control", "no-store");
  response.end(body);
}
