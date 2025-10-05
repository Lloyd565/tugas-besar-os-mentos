![Credit: 朧月](mascot.jpg)

## MentOS

## Milestone-0

Milestone 0 ini berfokus pada **implementasi fungsi pengaturan kernel** dan **konfigurasi Global Descriptor Table (GDT)** untuk mempersiapkan sistem memasuki *Protected Mode* dengan benar.

##### WHAT'S NEW?	

1. `src/kernel.c` : Implementasi fungsi pada kernel
2. **`src/stdlib/gdt.c`** : Implementasi struktur data GDT dan GDTR.

## Penjelasan Rinci

### 1. GDT

#### GDT adalah struktur data penting yang digunakan CPU dalam *Protected Mode* untuk mendefinisikan dan mengelola **segmen memori** (seperti batas, alamat basis, dan izin akses) untuk kode dan data kernel.

### 2. Kernel Setup

#### **Fungsi:**  `void kernel_setup(void)` adalah **fungsi C pertama** yang dieksekusi setelah *entrypoint* assembly berhasil mengaktifkan *Protected Mode* dan melakukan  *far jump* .
