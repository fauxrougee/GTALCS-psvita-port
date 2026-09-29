/* vita-elf-create appends SCE imports/module metadata to the executable
 * segment after linking. Keep an explicit gap before writable data: page
 * alignment alone can leave fewer bytes than the imports require. */
SECTIONS
{
  . = ALIGN(0x1000) + 0x2000;
}
INSERT BEFORE .init_array;
