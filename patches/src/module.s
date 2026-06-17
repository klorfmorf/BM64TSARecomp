.include "macro.inc"

.set noat
.set noreorder

.section .recomp_patch, "ax"

glabel modulegetpages
    addiu   $sp, $sp, -0x18
    sw      $ra, 0x14($sp)

    # Original local ABI:
    #   a0 = page_count
    #   a1 = vaddr
    #   s2 = module_id
    #
    # Standard C ABI for modulegetpages_impl:
    #   a0 = page_count
    #   a1 = vaddr
    #   a2 = module_id
    or      $a2, $s2, $zero

    jal     modulegetpages_impl
    nop

    lw      $ra, 0x14($sp)
    jr      $ra
    addiu  $sp, $sp, 0x18
endlabel modulegetpages