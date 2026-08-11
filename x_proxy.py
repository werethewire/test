#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
x_proxy.py — Word Rain 用の X (Twitter) API ローカルプロキシ

ブラウザから api.x.com を直接叩くと CORS で弾かれ、Bearer トークンも
ページに埋め込むことになる。そのため取得はこのプロセスが行い、
本文テキストの配列だけをブラウザへ返す。

使い方:
    setx X_BEARER_TOKEN "AAAA..."       (一度だけ。その後ターミナルを開き直す)
    python x_proxy.py                    -> http://127.0.0.1:8787

トークンは環境変数 X_BEARER_TOKEN、または同じフォルダの token.txt から読む。

エンドポイント:
    GET /health   起動確認
    GET /diag     何が原因で取得できないかを日本語で返す
    GET /search?q=...&max_results=100

注意: /2/tweets/search/recent は X API の有料 tier のエンドポイント。
無料枠のトークンでは 403 が返る。その場合はアプリ側の「Mastodon」
（認証不要）か「内蔵」「テキスト」ソースを使うこと。
"""

import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HOST = "127.0.0.1"
PORT = 8787
API_HOSTS = ["api.x.com", "api.twitter.com"]   # 前者が引けない環境があるので順に試す
PATH = "/2/tweets/search/recent"
HERE = os.path.dirname(os.path.abspath(__file__))


def load_token():
    raw = os.environ.get("X_BEARER_TOKEN", "")
    if not raw.strip():
        path = os.path.join(HERE, "token.txt")
        if os.path.exists(path):
            with open(path, "r", encoding="utf-8-sig") as f:
                raw = f.read()
    tok = raw.strip().strip('"').strip("'").strip()
    # "Bearer AAAA..." を丸ごと貼られても通す
    if tok.lower().startswith("bearer "):
        tok = tok[7:].strip()
    return tok


TOKEN = load_token()


def call_api(query, max_results):
    """(status, parsed_json_or_text, used_host) を返す。例外は投げない。"""
    params = urllib.parse.urlencode({
        "query": query,
        "max_results": max(10, min(100, max_results)),
        "tweet.fields": "lang",
    })
    last_err = None
    for host in API_HOSTS:
        url = "https://%s%s?%s" % (host, PATH, params)
        req = urllib.request.Request(url, headers={
            "Authorization": "Bearer " + TOKEN,
            "User-Agent": "word-rain-proxy/1.1",
        })
        try:
            with urllib.request.urlopen(req, timeout=20) as res:
                return res.status, json.loads(res.read().decode("utf-8")), host
        except urllib.error.HTTPError as e:
            body = e.read().decode("utf-8", "replace")
            try:
                body = json.loads(body)
            except ValueError:
                pass
            return e.code, body, host          # 応答は得られたので他ホストは試さない
        except Exception as e:
            last_err = e                        # 名前解決・接続エラーなら次のホストへ
    return 0, str(last_err), None


def explain(code, body):
    """HTTP ステータスを人が読める原因に翻訳する。"""
    if code == 200:
        return True, "OK"
    if code == 0:
        return False, "X に接続できません（ネットワーク / プロキシ / DNS を確認）: %s" % body
    if code == 401:
        return False, "トークンが無効です (401)。Bearer トークンを取り直してください"
    if code == 403:
        return False, ("権限がありません (403)。/2/tweets/search/recent は有料 tier の"
                       "エンドポイントで、無料枠のトークンでは読めません。"
                       "アプリ側の「Mastodon」ソース（認証不要）に切り替えてください")
    if code == 429:
        return False, "レート制限です (429)。しばらく待ってから再取得してください"
    if code == 400:
        detail = ""
        if isinstance(body, dict):
            detail = str(body.get("errors") or body.get("detail") or "")[:200]
        return False, "クエリが不正です (400)。検索クエリを見直してください %s" % detail
    return False, "X API が %d を返しました: %s" % (code, str(body)[:200])


class Handler(BaseHTTPRequestHandler):
    server_version = "WordRainProxy/1.1"
    # HTTP/1.0 のままにして毎レスポンスで接続を閉じる（keep-alive のハングを避ける）

    def _send(self, code, payload):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "*")
        # 公開ホストの HTTPS ページから 127.0.0.1 を叩く場合、Chrome は
        # Private Network Access の preflight でこのヘッダを要求する。
        self.send_header("Access-Control-Allow-Private-Network", "true")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Content-Length", "0")
        self.send_header("Connection", "close")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        qs = urllib.parse.parse_qs(parsed.query)

        if parsed.path == "/health":
            self._send(200, {"ok": True, "token": bool(TOKEN)})
            return

        if parsed.path == "/diag":
            if not TOKEN:
                self._send(200, {
                    "ok": False,
                    "verdict": ("診断: Bearer トークンが読めていません。環境変数 X_BEARER_TOKEN を設定して "
                                "x_proxy.py を起動し直すか、%s に token.txt を置いてください" % HERE),
                })
                return
            code, body, host = call_api("the", 10)
            ok, msg = explain(code, body)
            n = len(body.get("data", [])) if ok and isinstance(body, dict) else 0
            self._send(200, {
                "ok": ok,
                "code": code,
                "host": host,
                "verdict": ("診断: 正常に取得できています（%s から %d 件）" % (host, n)) if ok else ("診断: " + msg),
            })
            return

        if parsed.path != "/search":
            self._send(404, {"error": "unknown endpoint: " + parsed.path})
            return

        if not TOKEN:
            self._send(400, {
                "error": "no token",
                "hint": "Bearer トークンが設定されていません（環境変数 X_BEARER_TOKEN か token.txt）",
            })
            return

        query = (qs.get("q") or ["lang:ja -is:retweet"])[0]
        try:
            n = int((qs.get("max_results") or ["100"])[0])
        except ValueError:
            n = 100

        code, body, host = call_api(query, n)
        ok, msg = explain(code, body)
        if not ok:
            self._send(code if 400 <= code < 600 else 502, {"error": msg, "hint": msg, "code": code})
            return

        texts = [t.get("text", "") for t in body.get("data", [])]
        self._send(200, {"texts": texts, "count": len(texts), "host": host})

    def log_message(self, fmt, *args):
        sys.stderr.write("[proxy] %s\n" % (fmt % args))


class Server(ThreadingHTTPServer):
    # SO_REUSEADDR は Windows だと同じポートへの二重 bind を黙って許してしまい、
    # 「起動したのに古いプロセスが応答する」事故になる。明示的に無効化する。
    allow_reuse_address = False
    daemon_threads = True


def main():
    try:
        srv = Server((HOST, PORT), Handler)
    except OSError as e:
        print("ポート %d を bind できません: %s" % (PORT, e))
        print("既に起動しているプロキシがないか確認してください。")
        sys.exit(1)

    print("Word Rain X proxy -> http://%s:%d" % (HOST, PORT))
    if TOKEN:
        print("トークン: 読み込み済み (%d 文字, 先頭 %s...)" % (len(TOKEN), TOKEN[:6]))
    else:
        print("トークン: なし。/diag を叩くと設定方法を返します。")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n停止します")
    finally:
        srv.server_close()


if __name__ == "__main__":
    main()
