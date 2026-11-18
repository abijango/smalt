"""Run the mock: python3 -m mock_bookorbit --port 8765 [--ssl-certfile F --ssl-keyfile K]

Prints "listening <port>" on stdout once it accepts connections. Port 0 picks a
free port and prints the one it got.
"""
import argparse
import asyncio
import sys

import uvicorn

from .app import create_app


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="mock_bookorbit")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--ssl-certfile")
    parser.add_argument("--ssl-keyfile")
    args = parser.parse_args(argv)

    config = uvicorn.Config(create_app(), host=args.host, port=args.port, log_level="critical",
                            ssl_certfile=args.ssl_certfile, ssl_keyfile=args.ssl_keyfile,
                            timeout_keep_alive=30, access_log=False)
    server = uvicorn.Server(config)

    async def run():
        task = asyncio.ensure_future(server.serve())
        while not server.started and not task.done():
            await asyncio.sleep(0.02)
        if server.started:
            port = server.servers[0].sockets[0].getsockname()[1]
            print(f"listening {port}", flush=True)
        await task

    try:
        asyncio.run(run())
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
