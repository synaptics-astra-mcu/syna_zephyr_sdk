# subimage: bootinfo: xspi

# Command and arguments

product_dir="tools/scripts/image/xspi/"

gen_4le_bytes() {
  local s_b1=$(echo $1 | cut -b 7-8)
  local s_b2=$(echo $1 | cut -b 5-6)
  local s_b3=$(echo $1 | cut -b 3-4)
  local s_b4=$(echo $1 | cut -b 1-2)
  local out=$2

  echo "${s_b1}${s_b2}${s_b3}${s_b4}" | xxd -r -p > ${out}
}

get_pt_name() {
  local line=$1
  local ptfile=$2

  head -n ${line} ${ptfile} | tail -n 1 | awk '{print $2}'
}

to_bytes() {
  local size=$1
  local unit=${size: -1}
  local num=${size%[KMG]}

  case $unit in
    K) echo $((num * 1024)) ;;
    M) echo $((num * 1024 * 1024)) ;;
    G) echo $((num * 1024 * 1024 * 1024)) ;;
    *) echo ${num} ;;
  esac
}

get_pt_size() {
  local line=$1

  local size

  size=$(head -n ${line} ${product_dir}/xspi.pt | tail -n 1 | awk '{print $1}')

  to_bytes ${size}
}

get_pt_id() {
  local name=$1

  case "$name" in
    "sysmgr" | "sysmgr_a" | "sysmgr_b") echo "0x00000012" ;;
    "tzk" | "tzk_a" | "tzk_b") echo "0x00020014" ;;
    "bl" | "bl_a" | "bl_b" | "uboot") echo "0x00020017" ;;
    *) echo "0xFFFFFFFF" ;; #invalid value
  esac
}

gen_boot_entry() {
  local pt_offset=$1
  local pt_id=$2
  local pt_size=$3
  local flash_offset=$4
  local out=$5

  # change the size to KB
  local pt_size_inkb=$((${pt_size} / 1024))

  gen_4le_bytes $(printf "%08x" ${pt_offset}) ${intermediate_dir}/tmp1
  gen_4le_bytes $(printf "%08x" ${pt_id}) ${intermediate_dir}/tmp2
  gen_4le_bytes $(printf "%08x" ${pt_size_inkb}) ${intermediate_dir}/tmp3
  gen_4le_bytes $(printf "%08x" ${flash_offset}) ${intermediate_dir}/tmp4

  cat ${intermediate_dir}/tmp1 ${intermediate_dir}/tmp2 \
      ${intermediate_dir}/tmp3 ${intermediate_dir}/tmp4 \
      > ${intermediate_dir}/${out}
}

gen_boot_pt() {
  local pt_offset=$1
  local flash_offset=$2
  local out=$3

  # K0_SYNA
  gen_boot_entry ${pt_offset} 0x00000001 4096 ${flash_offset} tmpentry
  mv ${intermediate_dir}/tmpentry ${intermediate_dir}/${out}
  # K0_OEM
  gen_boot_entry $((${pt_offset} + 16)) 0x00000002 4096 $((${flash_offset} + 4096)) tmpentry
  cat ${intermediate_dir}/tmpentry >> ${intermediate_dir}/${out}
  # K0_3rd
  gen_boot_entry $((${pt_offset} + 16 * 2)) 0x00000003 4096 $((${flash_offset} + 4096 * 2)) tmpentry
  cat ${intermediate_dir}/tmpentry >> ${intermediate_dir}/${out}
  # K1_A
  gen_boot_entry $((${pt_offset} + 16 * 3 )) 0x00000004 4096 $((${flash_offset} + 4096 * 3)) tmpentry
  cat ${intermediate_dir}/tmpentry >> ${intermediate_dir}/${out}
  # K1_B
  gen_boot_entry $((${pt_offset} + 16 * 4)) 0x00000005 4096 $((${flash_offset} + 4096 * 4)) tmpentry
  cat ${intermediate_dir}/tmpentry >> ${intermediate_dir}/${out}
  # K1_C
  gen_boot_entry $((${pt_offset} + 16 * 5)) 0x00000006 4096 $((${flash_offset} + 4096 * 5)) tmpentry
  cat ${intermediate_dir}/tmpentry >> ${intermediate_dir}/${out}
  # K1_D
  gen_boot_entry $((${pt_offset} + 16 * 6)) 0x00000007 4096 $((${flash_offset} + 4096 * 6)) tmpentry
  cat ${intermediate_dir}/tmpentry >> ${intermediate_dir}/${out}
  # SPK
  gen_boot_entry $((${pt_offset} + 16 * 7)) 0x00000010 0x20000 $((${flash_offset} + 4096 * 7)) tmpentry
  cat ${intermediate_dir}/tmpentry >> ${intermediate_dir}/${out}
}

intermediate_dir=${outdir_subimg_intermediate}/bootinfo

# get the flash attribute
flash_attr=${product_dir}/flash_attr/${CONFIG_FLASH_ATTR}

[ -f ${flash_attr} ]

mkdir -p ${intermediate_dir}

cp ${flash_attr} ${intermediate_dir}/flash_attr

# append Partition_table_MCU_partB_offset
if [ "is${CONFIG_PREBOOT_BOOTFLOW_AB}" = "isy" ]; then
gen_4le_bytes 000040A0 ${intermediate_dir}/mcupartBoffset
else
gen_4le_bytes FFFFFFFF ${intermediate_dir}/mcupartBoffset
fi

cat ${intermediate_dir}/mcupartBoffset >> ${intermediate_dir}/flash_attr

# boot partition indicator (SPK and M52). boot partition is set to A by default
gen_4le_bytes FFFF0000 ${intermediate_dir}/bp_indicate_spk_vidx0
gen_4le_bytes FFFFFFFF ${intermediate_dir}/bp_indicate_spk_vidx1
cat ${intermediate_dir}/bp_indicate_spk_vidx0 ${intermediate_dir}/bp_indicate_spk_vidx1 \
> ${intermediate_dir}/bp_indicate_spk
truncate --size=4096 ${intermediate_dir}/bp_indicate_spk

gen_4le_bytes FFFF0000 ${intermediate_dir}/bp_indicate_m52_vidx0
gen_4le_bytes FFFFFFFF ${intermediate_dir}/bp_indicate_m52_vidx1
cat ${intermediate_dir}/bp_indicate_m52_vidx0 ${intermediate_dir}/bp_indicate_m52_vidx1 \
> ${intermediate_dir}/bp_indicate_m52
truncate --size=4096 ${intermediate_dir}/bp_indicate_m52

# generate paritition table
# 1. offset in PT of KO_SYNA_A is always 0x00000000
# 2. offset of K0_SYNA_B is always at 0x000000A0 if B exists
# 3. 0ffset of other images is started from 0x00000140 or 0x000000A0 if no B

# get info of bootinfo
pt_name=$(get_pt_name 2 ${product_dir}/xspi.pt)

if [ "${pt_name}" != "bootinfo" ]; then
  echo "bootinfo is a must for xSPI image"
  exit 1
fi

lines=$(cat ${product_dir}/xspi.pt | wc -l)

pt_name=""
pt_size=0
flash_offset=0
pt_offset=0
entry_num=0

# process A slot
valid_a_slot="tzk|tzk_a|bl|bl_a"

if [ "is${CONFIG_PREBOOT_BOOTFLOW_AB}" = "isy" ]; then
  pt_offset=0x00000140
  entry_num=20
else
  pt_offset=0x000000A0
  entry_num=10
fi

## append AP part A offset to flash_attr
ap_partA_offset=$((${pt_offset} + 0x4000))
gen_4le_bytes $(printf "%08x" ${ap_partA_offset}) ${intermediate_dir}/appartAoffset
cat ${intermediate_dir}/appartAoffset >> ${intermediate_dir}/flash_attr

rm -f ${intermediate_dir}/pt_a_normal

for ((i=2; i<=${lines}; i++)); do
  pt_name=$(get_pt_name ${i} ${product_dir}/xspi.pt)
  pt_size=$(get_pt_size ${i})

  # caculate the rest space for userdata
  if [ "$pt_size" = "-" ]; then
    pt_size=$((${CONFIG_XSPI_TOTAL_SIZE} - ${flash_offset}))
  fi

  echo "$pt_name: size(${pt_size}) flash offset(${flash_offset})"

  case "$pt_name" in
    "preboot" | "preboot_a")
      gen_boot_pt 0x0 ${flash_offset} pt_a
      apbl_size=$((${pt_size} - 0x27000))
      apbl_offset=$((${flash_offset} + 0x27000))
      gen_boot_entry 0x80 0x00000011 ${apbl_size} ${apbl_offset} pt_a_apbl_entry
      ;;
    "sysmgr" | "sysmgr_a")
      gen_boot_entry 0x90 0x00000012 ${pt_size} ${flash_offset} pt_a_sysmgr_entry
      ;;
    tzk|tzk_a|bl|bl_a|uboot)
    #$valid_a_slot)
      pt_id=$(get_pt_id ${pt_name})
      gen_boot_entry ${pt_offset} ${pt_id} ${pt_size} ${flash_offset} $pt_name
      cat ${intermediate_dir}/$pt_name >> ${intermediate_dir}/pt_a_normal
      pt_offset=$((${pt_offset} + 16))
      entry_num=$((${entry_num} + 1))
      ;;
  esac

  flash_offset=$((${flash_offset} + ${pt_size}))
done

#echo "${pt_offset} ${entry_num}"

if [ "is${CONFIG_PREBOOT_BOOTFLOW_AB}" = "isy" ]; then
# process B slot
flash_offset=0

## append AP part B offset to flash_attr
ap_partB_offset=$((${pt_offset} + 0x4000))
gen_4le_bytes $(printf "%08x" ${ap_partB_offset}) ${intermediate_dir}/appartBoffset
cat ${intermediate_dir}/appartBoffset >> ${intermediate_dir}/flash_attr

rm -f ${intermediate_dir}/pt_b_normal

for ((i=2; i<=${lines}; i++)); do
  pt_name=$(get_pt_name ${i} ${product_dir}/xspi.pt)
  pt_size=$(get_pt_size ${i})

  # caculate the rest space for userdata
  if [ "$pt_size" = "-" ]; then
    pt_size=$((${CONFIG_XSPI_TOTAL_SIZE} - ${flash_offset}))
  fi

  #echo "$pt_name: size(${pt_size}) flash offset(${flash_offset})"

  case "$pt_name" in
    "preboot_b")
      gen_boot_pt 0xA0 ${flash_offset} pt_b
      apbl_size=$((${pt_size} - 0x27000))
      apbl_offset=$((${flash_offset} + 0x27000))
      gen_boot_entry 0x120 0x00000011 ${apbl_size} ${apbl_offset} pt_b_apbl_entry
      ;;
    "sysmgr_b")
      gen_boot_entry 0x130 0x00000012 ${pt_size} ${flash_offset} pt_b_sysmgr_entry
      ;;
    tzk_b|bl_b)
      pt_id=$(get_pt_id ${pt_name})
      gen_boot_entry ${pt_offset} ${pt_id} ${pt_size} ${flash_offset} $pt_name
      cat ${intermediate_dir}/$pt_name >> ${intermediate_dir}/pt_b_normal
      pt_offset=$((${pt_offset} + 16))
      entry_num=$((${entry_num} + 1))
      ;;
  esac

  flash_offset=$((${flash_offset} + ${pt_size}))
done
else
## append AP part B offset to flash_attr
gen_4le_bytes FFFFFFFF ${intermediate_dir}/appartBoffset
cat ${intermediate_dir}/appartBoffset >> ${intermediate_dir}/flash_attr
fi

#echo "${pt_offset} ${entry_num}"

# process other partitions
rm -f ${intermediate_dir}/pt_common
flash_offset=0

for ((i=2; i<=${lines}; i++)); do
  pt_name=$(get_pt_name ${i} ${product_dir}/xspi.pt)
  pt_size=$(get_pt_size ${i})

  # caculate the rest space for userdata
  if [ "$pt_size" = "-" ]; then
    pt_size=$((${CONFIG_XSPI_TOTAL_SIZE} - ${flash_offset}))
  fi

  #echo "$pt_name: size(${pt_size}) flash offset(${flash_offset})"

  case "$pt_name" in
    factory_setting|misc|userdata)
      pt_id=$(get_pt_id ${pt_name})
      gen_boot_entry ${pt_offset} ${pt_id} ${pt_size} ${flash_offset} $pt_name
      cat ${intermediate_dir}/$pt_name >> ${intermediate_dir}/pt_common
      pt_offset=$((${pt_offset} + 16))
      entry_num=$((${entry_num} + 1))
      ;;
  esac

  flash_offset=$((${flash_offset} + ${pt_size}))
done

#echo "${pt_offset} ${entry_num}"

## append number of entry to flash_attr
gen_4le_bytes $(printf "%08x" ${entry_num}) ${intermediate_dir}/entry_num
cat ${intermediate_dir}/entry_num >> ${intermediate_dir}/flash_attr

## append crc to the end of flash_attr
${CONFIG_SYNA_SDK_OUT_HOST_REL_PATH}/crc -a ${intermediate_dir}/flash_attr

truncate --size=4096 ${intermediate_dir}/flash_attr

cat ${intermediate_dir}/flash_attr \
    ${intermediate_dir}/flash_attr \
    ${intermediate_dir}/bp_indicate_spk \
    ${intermediate_dir}/bp_indicate_m52 \
    > ${intermediate_dir}/bootinfo.bin

cat ${intermediate_dir}/pt_a_apbl_entry >> ${intermediate_dir}/pt_a
cat ${intermediate_dir}/pt_a_sysmgr_entry >> ${intermediate_dir}/pt_a
cat ${intermediate_dir}/pt_a >> ${intermediate_dir}/bootinfo.bin

if [ "is${CONFIG_PREBOOT_BOOTFLOW_AB}" = "isy" ]; then
  cat ${intermediate_dir}/pt_b_apbl_entry >> ${intermediate_dir}/pt_b
  cat ${intermediate_dir}/pt_b_sysmgr_entry >> ${intermediate_dir}/pt_b
  cat ${intermediate_dir}/pt_b >> ${intermediate_dir}/bootinfo.bin
fi

cat ${intermediate_dir}/pt_a_normal >> ${intermediate_dir}/bootinfo.bin

if [ "is${CONFIG_PREBOOT_BOOTFLOW_AB}" = "isy" ]; then
  cat ${intermediate_dir}/pt_b_normal >> ${intermediate_dir}/bootinfo.bin
fi

if [ -f "${intermediate_dir}/pt_common" ]; then
  cat ${intermediate_dir}/pt_common >> ${intermediate_dir}/bootinfo.bin
fi

if [ "is${CONFIG_UBOOT_SPIUBOOT}" = "isy" ]; then
  truncate --size=20480 ${intermediate_dir}/bootinfo.bin
fi

cp -ad ${intermediate_dir}/bootinfo.bin ${outdir_subimg_intermediate}/bootinfo.subimg
