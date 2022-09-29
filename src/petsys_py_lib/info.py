# kate: indent-mode: python; indent-pasted-text false; indent-width 8; replace-tabs: off;
# vim: tabstop=8 shiftwidth=8

def is_febd(d):
	base_pcb, fw_variant, prom_variant = d

	fw_variant = (fw_variant >> 48) & 0xFFFF

	if base_pcb == 0xFFFF: return False
	if fw_variant in [ 0x0000, 0x0001]: return True
	return False

def is_trigger(d):
	base_pcb, fw_variant, prom_variant = d
	fw_variant = (fw_variant >> 48) & 0xFFFF

	if base_pcb == 0xFFFF: return False
	if fw_variant in [ 0x0001, 0x0002]: return True
	return False

def allows_legacy_module(d):
	base_pcb, fw_variant, prom_variant = d

	if base_pcb != 0x0002: return False
	fw_variant = (fw_variant >> 32) & 0xFFFF
	if fw_variant in [ 0x0001, 0x0011 ]: return True
	return False



def fem_per_febd(d):
	base_pcb, fw_variant, prom_variant = d


	if base_pcb in [ 0x0000, 0x0001, 0x0002 ]:
		return 8
	elif base_pcb in [ 0x0003 ]:
		return 1
	elif base_pcb in [ 0x0004 ]:
		return 2
	elif base_pcb in [ 0x0005 ]:
		return 16
	else:
		return None

def asic_per_module(d):
	base_pcb, fw_variant, prom_variant = d
	fw_variant = (fw_variant >> 32) & 0xFFFF
	if   fw_variant == 0x0000: return 2
	elif fw_variant == 0x0001: return 2
	elif fw_variant == 0x0002: return 5
	elif fw_variant == 0x0011: return 2
	elif fw_variant == 0x0111: return 4
	elif fw_variant == 0x0211: return 8
	else: return None

def bias_slots(d):
	if not is_febd(d):
		return None

	base_pcb, fw_variant, prom_variant = d

	if base_pcb == 0x0000:
		return 1
	elif base_pcb == 0x0001:
		return 1
	elif base_pcb == 0x0002:
		return 1
	elif base_pcb == 0x0003:
		return 0
	elif base_pcb == 0x0004:
		return 0
	elif base_pcb == 0x0005:
		return 2
	else:
		return None

