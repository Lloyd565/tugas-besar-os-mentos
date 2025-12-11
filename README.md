![Credit: 朧月](mascot.jpg)

# MentOS — Tugas Besar IF2130 Sistem Operasi (2025/2026)
Dokumentasi singkat untuk proyek OS sederhana ini (template dasar dan fitur tugas besar).

**Nama Kelompok**

Kelompok: `Mentos`

**Tabel Anggota**

| Nama                         | NIM (dummy) |
|----------------------------- |------------:|
| Kurt Mikhael Purba           |     13524065 |
| Nathan E. Marpaung           |     13524065 |
| Angelina Andra Alanna        |     13524079 |
| Richard Samuel Simanullang   |     13524112 |

**Daftar Isi**
- Cara Run
- Fitur yang Dibuat
- Contoh Pemakaian (quick tests)
- Batasan / Catatan
- Maskot

---

**Cara Run**

Pastikan berada di direktori root proyek (mengandung `Makefile`). Cukup jalankan perintah berikut untuk build, insert binaries ke disk image, dan menjalankan OS di QEMU:

```bash
make all
```

Catatan: `make all` menjalankan rangkaian target yang mencakup `clean`, `disk`, `insert-shell`, `insert-clock`, dan `run`.

---

**Fitur yang Dibuat (ringkasan)**
- Shell interaktif dengan prompt: `root@MentOS2130 <cwd> $ `
- Perintah dasar filesystem: `ls`, `cd`, `pwd`, `mkdir`, `rm`, `mv`, `cp`, `cat`, `touch` (dengan opsi menulis konten dari satu baris)
- `touch` dapat membuat file kosong atau menulis konten dari sisa baris perintah: `touch file.txt some content here`
- Pencarian teks: `grep <pattern> <file>` dan dukungan pipe: `cmd | grep pattern`
- `find` (rekursif) untuk mencari file dengan nama tertentu
- Proses userland: `exec` (jalankan program), `ps`, `kill`, `clock` (program contoh)
- Piping antar-perintah (simple `|`), dan mode pipe buffer internal
- Mouse support + selection: pilih teks pada layar dengan drag, salin (Ctrl+Shift+C) dan paste (Ctrl+Shift+V) ke input
- Speaker / audio: `beep`, `speaker <freq> <duration>`, `play <musicfile>` (pemain musik sederhana)
- Help: `help` menampilkan daftar perintah yang tersedia

---

**Contoh Pemakaian & Quick Tests**

1) Test `touch` menulis konten:
```text
root@MentOS2130 / $ touch test.txt Hello world
root@MentOS2130 / $ cat test.txt
Hello world
```

2) Test `grep` literal substring (case-sensitive):
```text
root@MentOS2130 / $ grep Hello test.txt
Hello world

# Pipe example:
root@MentOS2130 / $ ls | grep test
test.txt
```

3) Membuat direktori dan pindah:
```text
root@MentOS2130 / $ mkdir docs
root@MentOS2130 / $ cd docs
root@MentOS2130 /docs $ pwd
/docs
```

4) Copy dan overwrite:
```text
root@MentOS2130 / $ cp test.txt copy.txt
root@MentOS2130 / $ cat copy.txt
Hello world
```

---

**Batasan / Catatan**
- Parser shell saat ini memisahkan token berdasarkan spasi; tidak ada dukungan quoting untuk nama file atau pattern yang mengandung spasi.
- `grep` melakukan pencarian literal substring (bukan regex) dan case-sensitive.
- `touch` mengambil konten sebagai semua teks setelah nama file pada baris perintah (tidak ada escaping/quotes parsing).
- Ukuran file/capacity dibatasi ke `BLOCK_COUNT * BLOCK_SIZE` bytes (lihat `src` untuk nilai). Jika konten lebih besar, akan terpotong.

---

**Maskot**
Gambar: `mascot.jpg` (credit: 朧月)

---

