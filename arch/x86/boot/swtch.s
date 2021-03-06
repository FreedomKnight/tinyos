; Context switch
;
;   void swtch(struct context **old, struct context *new);
;
; Save current register context in old
; and then load register context from new.

[GLOBAL swtch]
swtch:
  mov eax, [esp + 4] ;4(%esp), %eax
  mov edx, [esp + 8] ;8(%esp), %edx
  cmp [eax], edx
  je swtch_end

  ; Save old callee-save registers
  push ebp
  push ebx
  push esi
  push edi

  ; Switch stacks
  mov [eax], esp ;%esp, (%eax)
  mov esp, edx ;%edx, %esp

  ; Load new callee-save registers
  pop edi
  pop esi
  pop ebx
  pop ebp
swtch_end:
  ret

[GLOBAL load_context]
load_context:
  mov eax, [esp + 4] ;4(%esp), %eax

  ; Switch stacks
  mov esp, eax

  ; Load new callee-save registers
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
