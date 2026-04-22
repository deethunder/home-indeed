"""
init-lyrics-db.py
-----------------
Creates (or re-initialises) data/homein-lyrics.db with the correct schema.

Run this once from the project root before building:
    python tools/init-lyrics-db.py

The schema here must stay in sync with HomeInLyricsDB::EnsureSchema() in
src/database/homein-lyrics-db.cpp — both define the same tables and triggers.
"""

import sqlite3
import os

DB_PATH = "data/homein-lyrics.db"


def init_db():
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    # Main lyrics table
    c.execute("""
        CREATE TABLE IF NOT EXISTS lyrics (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            title        TEXT    NOT NULL,
            artist       TEXT,
            content      TEXT    NOT NULL,
            source       TEXT,
            last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    """)

    # FTS5 external-content table — keeps the index in sync with lyrics.
    # content=lyrics means FTS reads from the lyrics table for highlight/snippet.
    # content_rowid=id links FTS rowids to lyrics.id.
    c.execute("""
        CREATE VIRTUAL TABLE IF NOT EXISTS lyrics_fts USING fts5(
            title,
            artist,
            content,
            content='lyrics',
            content_rowid='id'
        )
    """)

    # Triggers to keep FTS automatically in sync on INSERT / DELETE / UPDATE.
    # These mirror the triggers in HomeInLyricsDB::EnsureSchema().
    c.execute("""
        CREATE TRIGGER IF NOT EXISTS lyrics_ai AFTER INSERT ON lyrics BEGIN
            INSERT INTO lyrics_fts(rowid, title, artist, content)
                VALUES (new.id, new.title, new.artist, new.content);
        END
    """)

    c.execute("""
        CREATE TRIGGER IF NOT EXISTS lyrics_ad AFTER DELETE ON lyrics BEGIN
            INSERT INTO lyrics_fts(lyrics_fts, rowid, title, artist, content)
                VALUES ('delete', old.id, old.title, old.artist, old.content);
        END
    """)

    c.execute("""
        CREATE TRIGGER IF NOT EXISTS lyrics_au AFTER UPDATE ON lyrics BEGIN
            INSERT INTO lyrics_fts(lyrics_fts, rowid, title, artist, content)
                VALUES ('delete', old.id, old.title, old.artist, old.content);
            INSERT INTO lyrics_fts(rowid, title, artist, content)
                VALUES (new.id, new.title, new.artist, new.content);
        END
    """)

    conn.commit()

    # Verify
    c.execute("SELECT name FROM sqlite_master WHERE type IN ('table','trigger') ORDER BY name")
    objects = [row[0] for row in c.fetchall()]
    print(f"Lyrics DB initialised at: {DB_PATH}")
    print(f"Objects created: {', '.join(objects)}")

    conn.close()


if __name__ == "__main__":
    init_db()
