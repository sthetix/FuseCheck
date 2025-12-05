# TegraExplorer Prodinfo Dumping Implementation Analysis

## Overview
This document provides a comprehensive analysis of how TegraExplorer implements prodinfo dumping, which can be ported to Lockpick_RCM.

## Key Files Involved

Main Implementation Files:
- gptmenu.c: Entry point for partition dumping UI (source/storage/gptmenu.c)
- emmcfile.c: Core dumping/writing logic (source/storage/emmcfile.c)
- nx_emmc.c: GPT parsing and partition management (source/storage/nx_emmc.c)
- nx_emmc_bis.c: BIS key encryption/decryption engine (source/storage/nx_emmc_bis.c)
- mountmanager.c: MMC connection and mounting (source/storage/mountmanager.c)
- keys.c: Key derivation and dumping (source/keys/keys.c)

## Prodinfo Partition Details

Partition Index  Name        Encrypted  Key Slot (Crypt)  Size
0                PRODINFO    YES        0                 ~32MB
1                PRODINFOF   YES        0                 ~32MB (backup)
8                SAFE        YES        2                 ~8MB
9                SYSTEM      YES        4                 ~1.8GB
10               USER        YES        4                 Variable

PRODINFO uses ks_crypt=0 and ks_tweak=1 for XTS encryption.

## Complete Data Flow

### Step 1: Initial Setup and Key Derivation
DumpKeys() (keys.c:262) is called to:
1. Read PKG1 from NAND at offset 0x100000
2. Extract TSEC keys using TSEC firmware
3. Read keyblobs from offset 0x180000
4. Derive master key and device key
5. Derive BIS keys (3 sets) for partition encryption
6. Derive miscellaneous keys (header key, save mac key)

### Step 2: Connection and GPT Parsing
GptMenu(MMC_CONN_EMMC) calls connectMMC() which:
1. Initializes eMMC storage via emummc_storage_init_mmc()
2. Parses GPT from LBA 1 (33 blocks)
3. Populates partition list including PRODINFOF

### Step 3: User Selection
User selects PRODINFOF from partition menu.

### Step 4: Partition Dumping
DumpOrWriteEmmcPart() is called with:
- path: "sd:/tegraexplorer/Dumps/PRODINFOF"
- part: "PRODINFOF"
- write: 0 (dump mode)
- force: 0

### Step 5: Dumping Logic (emmcfile.c:147-186)

The DumpOrWriteEmmcPart function:
1. Finds partition in GPT using nx_emmc_part_find()
2. Sets NAND to user area (partition 0)
3. Checks if partition is encrypted via isSystemPartCrypt()
4. Initializes BIS encryption context with nx_emmc_bis_init()
5. Calls EmmcDumpToFile() with crypt=true

### Step 6: Encrypted Reading and File Writing

EmmcDumpToFile() (emmcfile.c:19-73):
- Loop through all sectors:
  1. Read up to TConf.FSBuffSize worth of sectors
  2. If encrypted: nx_emmc_bis_read(curLba, num, buff)
  3. Write to file: f_write(&fp, buff, size, NULL)
  4. Display progress percentage
- Close file

## NAND Operations

Raw Operations (no encryption):
- emummc_storage_read(): Reads raw sectors
- emummc_storage_write(): Writes raw sectors
- nx_emmc_part_read(): Reads partition with bounds checking
- nx_emmc_part_write(): Writes partition with bounds checking

Encrypted Operations:
- nx_emmc_bis_read(): Reads and decrypts via XTS
- nx_emmc_bis_write(): Encrypts and writes via XTS

## Encryption/Decryption System

### XTS-AES Implementation (nx_emmc_bis.c)

Core Functions:
1. _nx_aes_xts_crypt_sec(): Core XTS encryption/decryption
   - Takes tweak key slot, crypt key slot, encrypt flag, sector number
   - Uses galois field multiplication (_gf256_mul_x_le)
   - Returns 0 on success, 1 on error

2. nx_emmc_bis_read_block(): Reads and decrypts a single block
   - Implements cluster caching (16KB clusters)
   - 32 sectors per cluster (512 bytes × 32)
   - Maintains cluster lookup table for fast access
   - Generates tweak from sector number

3. nx_emmc_bis_init(): Initializes encryption for a partition
   - For PRODINFO (index 0/1): ks_crypt=0, ks_tweak=1
   - For SAFE (index 8): ks_crypt=2, ks_tweak=3
   - For SYSTEM/USER (index 9/10): ks_crypt=4, ks_tweak=5

### Key Parameters
- Sector Size: 512 bytes
- Cluster Size: 16KB = 32 sectors = 0x4000 bytes
- XTS Tweak: 128-bit per sector (derived from sector number)
- Max Cluster Cache: 32768 entries

## Key Management

### Key Derivation Chain
1. Extract from Hardware:
   - TSEC Keys (TSEC firmware execution)
   - SBK (Secure Boot Key from fuses)
   - Device Key (fuse-derived)

2. Derive BIS Keys:
   - Input: Device Key, Master Key, BIS Key Sources
   - Process: AES unwrap operations
   - Output: 3 sets of BIS keys (each 32 bytes)

3. Load to Security Engine:
   - Slot 0: BIS Key[0] first 16 bytes
   - Slot 1: BIS Key[0] second 16 bytes
   - Slot 2: BIS Key[1] first 16 bytes
   - Slot 3: BIS Key[1] second 16 bytes
   - Slot 4: BIS Key[2] first 16 bytes
   - Slot 5: BIS Key[2] second 16 bytes

### Key Slot Mapping (mountmanager.c:13-26)

SetKeySlots() function:
- se_aes_key_set(0, dumpedKeys.bis_key[0], AES_128_KEY_SIZE);
- se_aes_key_set(1, dumpedKeys.bis_key[0] + AES_128_KEY_SIZE, AES_128_KEY_SIZE);
- And similarly for slots 2-5 with bis_key[1] and bis_key[2]

## Implementation Specifics

### Encryption Check (emmcfile.c:134-145)
int isSystemPartCrypt(emmc_part_t *part){
    switch (part->index){
        case 0: // PRODINFO
        case 1: // PRODINFOF
        case 8: // SAFE
        case 9: // SYSTEM
        case 10: // USER
            return 1;
        default:
            return 0;
    }
}

### File Writing Loop (emmcfile.c:43-68)
while (totalSectors > 0){
    u32 num = MIN(totalSectors, TConf.FSBuffSize / NX_EMMC_BLOCKSIZE);
    
    int readRes = 0;
    if (crypt)
        readRes = !nx_emmc_bis_read(curLba, num, buff);
    else 
        readRes = emummc_storage_read(&emmc_storage, curLba, num, buff);
    
    if (!readRes){
        err = newErrCode(TE_ERR_EMMC_READ_FAIL);
        break;
    }
    
    if ((res = f_write(&fp, buff, num * NX_EMMC_BLOCKSIZE, NULL))){
        err = newErrCode(res);
        break;
    }
    
    curLba += num;
    totalSectors -= num;
    
    u32 percent = ((curLba - lba_start) * 100) / ((lba_end - lba_start + 1));
    gfx_printf("[%3d%%]", percent);
}

## Code Structure

### BIS Cache Structure
typedef struct {
    u32 cluster_num;        // Logical cluster index
    u32 visit_count;        // Access counter
    u8  dirty;              // Modification flag
    u8  align[7];
    u8  cluster[0x4000];    // 16KB data
} cluster_cache_t;

### Partition Structure
typedef struct _emmc_part_t {
    u32 index;              // Partition index (0 for PRODINFO, 1 for PRODINFOF)
    u32 lba_start;          // Starting LBA
    u32 lba_end;            // Ending LBA (inclusive)
    u64 attrs;              // Attributes
    char name[37];          // "PRODINFOF"
    link_t link;
} emmc_part_t;

## Porting Steps to Lockpick_RCM

1. Copy these files from TegraExplorer:
   - source/storage/nx_emmc.c and .h
   - source/storage/nx_emmc_bis.c and .h
   - source/storage/emmcfile.c and .h
   - source/storage/mountmanager.c and .h
   - source/keys/keys.c and .h

2. Adapt for Lockpick_RCM context:
   - Remove UI code (gptmenu.c not needed)
   - Focus only on PRODINFOF partition
   - Use existing key dumping logic

3. Create simple wrapper:
   int dump_prodinfo(const char *output_path) {
       if (connectMMC(MMC_CONN_EMMC))
           return -1;
       
       emmc_part_t *prodinfo = nx_emmc_part_find(GetCurGPT(), "PRODINFOF");
       if (!prodinfo)
           return -2;
       
       nx_emmc_bis_init(prodinfo);
       
       return DumpOrWriteEmmcPart(output_path, "PRODINFOF", 0, 0);
   }

4. Call from main key dumping flow
5. Verify output matches expected PRODINFO structure

## Key Technical Points for Porting

1. The BIS implementation handles XTS decryption transparently
2. Cluster caching improves performance for large partitions
3. Partition encryption keys are determined by partition index
4. PRODINFO requires proper key derivation first
5. Tweak generation is automatic based on sector number
6. No special handling needed for PRODINFO vs other encrypted partitions

