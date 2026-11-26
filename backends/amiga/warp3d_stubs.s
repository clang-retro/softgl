# warp3d_stubs.s - the calls to warp3d.library: each stub moves the C
# arguments to the registers of backends/amiga/warp3d.fd and calls the
# library vector. Made by tools/gen-warp3d-stubs.sh; do not edit.
#
# Part of clang-retro softgl, a software implementation of OpenGL.
# SPDX-License-Identifier: AGPL-3.0-only
	.section .text.sgl_W3D_CreateContext,"ax",@progbits
	.globl sgl_W3D_CreateContext
	.type sgl_W3D_CreateContext,@function
sgl_W3D_CreateContext:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-30,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_CreateContext,.-sgl_W3D_CreateContext
	.section .text.sgl_W3D_DestroyContext,"ax",@progbits
	.globl sgl_W3D_DestroyContext
	.type sgl_W3D_DestroyContext,@function
sgl_W3D_DestroyContext:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-36,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_DestroyContext,.-sgl_W3D_DestroyContext
	.section .text.sgl_W3D_GetState,"ax",@progbits
	.globl sgl_W3D_GetState
	.type sgl_W3D_GetState,@function
sgl_W3D_GetState:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-42,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_GetState,.-sgl_W3D_GetState
	.section .text.sgl_W3D_SetState,"ax",@progbits
	.globl sgl_W3D_SetState
	.type sgl_W3D_SetState,@function
sgl_W3D_SetState:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-48,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetState,.-sgl_W3D_SetState
	.section .text.sgl_W3D_CheckDriver,"ax",@progbits
	.globl sgl_W3D_CheckDriver
	.type sgl_W3D_CheckDriver,@function
sgl_W3D_CheckDriver:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l sgl_Warp3DBase,%a6
	jsr (-54,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_CheckDriver,.-sgl_W3D_CheckDriver
	.section .text.sgl_W3D_LockHardware,"ax",@progbits
	.globl sgl_W3D_LockHardware
	.type sgl_W3D_LockHardware,@function
sgl_W3D_LockHardware:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-60,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_LockHardware,.-sgl_W3D_LockHardware
	.section .text.sgl_W3D_UnLockHardware,"ax",@progbits
	.globl sgl_W3D_UnLockHardware
	.type sgl_W3D_UnLockHardware,@function
sgl_W3D_UnLockHardware:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-66,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_UnLockHardware,.-sgl_W3D_UnLockHardware
	.section .text.sgl_W3D_WaitIdle,"ax",@progbits
	.globl sgl_W3D_WaitIdle
	.type sgl_W3D_WaitIdle,@function
sgl_W3D_WaitIdle:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-72,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_WaitIdle,.-sgl_W3D_WaitIdle
	.section .text.sgl_W3D_CheckIdle,"ax",@progbits
	.globl sgl_W3D_CheckIdle
	.type sgl_W3D_CheckIdle,@function
sgl_W3D_CheckIdle:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-78,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_CheckIdle,.-sgl_W3D_CheckIdle
	.section .text.sgl_W3D_Query,"ax",@progbits
	.globl sgl_W3D_Query
	.type sgl_W3D_Query,@function
sgl_W3D_Query:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-84,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_Query,.-sgl_W3D_Query
	.section .text.sgl_W3D_GetTexFmtInfo,"ax",@progbits
	.globl sgl_W3D_GetTexFmtInfo
	.type sgl_W3D_GetTexFmtInfo,@function
sgl_W3D_GetTexFmtInfo:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-90,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_GetTexFmtInfo,.-sgl_W3D_GetTexFmtInfo
	.section .text.sgl_W3D_AllocTexObj,"ax",@progbits
	.globl sgl_W3D_AllocTexObj
	.type sgl_W3D_AllocTexObj,@function
sgl_W3D_AllocTexObj:
	lea (-8,%sp),%sp
	movem.l %a2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%a1
	move.l (20,%sp),%a2
	move.l sgl_Warp3DBase,%a6
	jsr (-96,%a6)
	movem.l (%sp),%a2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_AllocTexObj,.-sgl_W3D_AllocTexObj
	.section .text.sgl_W3D_FreeTexObj,"ax",@progbits
	.globl sgl_W3D_FreeTexObj
	.type sgl_W3D_FreeTexObj,@function
sgl_W3D_FreeTexObj:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-102,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_FreeTexObj,.-sgl_W3D_FreeTexObj
	.section .text.sgl_W3D_ReleaseTexture,"ax",@progbits
	.globl sgl_W3D_ReleaseTexture
	.type sgl_W3D_ReleaseTexture,@function
sgl_W3D_ReleaseTexture:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-108,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_ReleaseTexture,.-sgl_W3D_ReleaseTexture
	.section .text.sgl_W3D_FlushTextures,"ax",@progbits
	.globl sgl_W3D_FlushTextures
	.type sgl_W3D_FlushTextures,@function
sgl_W3D_FlushTextures:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-114,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_FlushTextures,.-sgl_W3D_FlushTextures
	.section .text.sgl_W3D_SetFilter,"ax",@progbits
	.globl sgl_W3D_SetFilter
	.type sgl_W3D_SetFilter,@function
sgl_W3D_SetFilter:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l (16,%sp),%d0
	move.l (20,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-120,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetFilter,.-sgl_W3D_SetFilter
	.section .text.sgl_W3D_SetTexEnv,"ax",@progbits
	.globl sgl_W3D_SetTexEnv
	.type sgl_W3D_SetTexEnv,@function
sgl_W3D_SetTexEnv:
	lea (-8,%sp),%sp
	movem.l %a2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%a1
	move.l (20,%sp),%d1
	move.l (24,%sp),%a2
	move.l sgl_Warp3DBase,%a6
	jsr (-126,%a6)
	movem.l (%sp),%a2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_SetTexEnv,.-sgl_W3D_SetTexEnv
	.section .text.sgl_W3D_SetWrapMode,"ax",@progbits
	.globl sgl_W3D_SetWrapMode
	.type sgl_W3D_SetWrapMode,@function
sgl_W3D_SetWrapMode:
	lea (-8,%sp),%sp
	movem.l %a2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%a1
	move.l (20,%sp),%d0
	move.l (24,%sp),%d1
	move.l (28,%sp),%a2
	move.l sgl_Warp3DBase,%a6
	jsr (-132,%a6)
	movem.l (%sp),%a2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_SetWrapMode,.-sgl_W3D_SetWrapMode
	.section .text.sgl_W3D_UpdateTexImage,"ax",@progbits
	.globl sgl_W3D_UpdateTexImage
	.type sgl_W3D_UpdateTexImage,@function
sgl_W3D_UpdateTexImage:
	lea (-12,%sp),%sp
	movem.l %a2/%a3/%a6,(%sp)
	move.l (16,%sp),%a0
	move.l (20,%sp),%a1
	move.l (24,%sp),%a2
	move.l (28,%sp),%d1
	move.l (32,%sp),%a3
	move.l sgl_Warp3DBase,%a6
	jsr (-138,%a6)
	movem.l (%sp),%a2/%a3/%a6
	lea (12,%sp),%sp
	rts
	.size sgl_W3D_UpdateTexImage,.-sgl_W3D_UpdateTexImage
	.section .text.sgl_W3D_UploadTexture,"ax",@progbits
	.globl sgl_W3D_UploadTexture
	.type sgl_W3D_UploadTexture,@function
sgl_W3D_UploadTexture:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-144,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_UploadTexture,.-sgl_W3D_UploadTexture
	.section .text.sgl_W3D_DrawLine,"ax",@progbits
	.globl sgl_W3D_DrawLine
	.type sgl_W3D_DrawLine,@function
sgl_W3D_DrawLine:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-150,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_DrawLine,.-sgl_W3D_DrawLine
	.section .text.sgl_W3D_DrawPoint,"ax",@progbits
	.globl sgl_W3D_DrawPoint
	.type sgl_W3D_DrawPoint,@function
sgl_W3D_DrawPoint:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-156,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_DrawPoint,.-sgl_W3D_DrawPoint
	.section .text.sgl_W3D_DrawTriangle,"ax",@progbits
	.globl sgl_W3D_DrawTriangle
	.type sgl_W3D_DrawTriangle,@function
sgl_W3D_DrawTriangle:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-162,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_DrawTriangle,.-sgl_W3D_DrawTriangle
	.section .text.sgl_W3D_DrawTriFan,"ax",@progbits
	.globl sgl_W3D_DrawTriFan
	.type sgl_W3D_DrawTriFan,@function
sgl_W3D_DrawTriFan:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-168,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_DrawTriFan,.-sgl_W3D_DrawTriFan
	.section .text.sgl_W3D_DrawTriStrip,"ax",@progbits
	.globl sgl_W3D_DrawTriStrip
	.type sgl_W3D_DrawTriStrip,@function
sgl_W3D_DrawTriStrip:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-174,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_DrawTriStrip,.-sgl_W3D_DrawTriStrip
	.section .text.sgl_W3D_SetAlphaMode,"ax",@progbits
	.globl sgl_W3D_SetAlphaMode
	.type sgl_W3D_SetAlphaMode,@function
sgl_W3D_SetAlphaMode:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d1
	move.l (16,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-180,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetAlphaMode,.-sgl_W3D_SetAlphaMode
	.section .text.sgl_W3D_SetBlendMode,"ax",@progbits
	.globl sgl_W3D_SetBlendMode
	.type sgl_W3D_SetBlendMode,@function
sgl_W3D_SetBlendMode:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-186,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetBlendMode,.-sgl_W3D_SetBlendMode
	.section .text.sgl_W3D_SetDrawRegion,"ax",@progbits
	.globl sgl_W3D_SetDrawRegion
	.type sgl_W3D_SetDrawRegion,@function
sgl_W3D_SetDrawRegion:
	lea (-8,%sp),%sp
	movem.l %a2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%a1
	move.l (20,%sp),%d1
	move.l (24,%sp),%a2
	move.l sgl_Warp3DBase,%a6
	jsr (-192,%a6)
	movem.l (%sp),%a2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_SetDrawRegion,.-sgl_W3D_SetDrawRegion
	.section .text.sgl_W3D_SetFogParams,"ax",@progbits
	.globl sgl_W3D_SetFogParams
	.type sgl_W3D_SetFogParams,@function
sgl_W3D_SetFogParams:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l (16,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-198,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetFogParams,.-sgl_W3D_SetFogParams
	.section .text.sgl_W3D_SetColorMask,"ax",@progbits
	.globl sgl_W3D_SetColorMask
	.type sgl_W3D_SetColorMask,@function
sgl_W3D_SetColorMask:
	lea (-12,%sp),%sp
	movem.l %d2/%d3/%a6,(%sp)
	move.l (16,%sp),%a0
	move.l (20,%sp),%d0
	move.l (24,%sp),%d1
	move.l (28,%sp),%d2
	move.l (32,%sp),%d3
	move.l sgl_Warp3DBase,%a6
	jsr (-204,%a6)
	movem.l (%sp),%d2/%d3/%a6
	lea (12,%sp),%sp
	rts
	.size sgl_W3D_SetColorMask,.-sgl_W3D_SetColorMask
	.section .text.sgl_W3D_SetStencilFunc,"ax",@progbits
	.globl sgl_W3D_SetStencilFunc
	.type sgl_W3D_SetStencilFunc,@function
sgl_W3D_SetStencilFunc:
	lea (-8,%sp),%sp
	movem.l %d2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%d0
	move.l (20,%sp),%d1
	move.l (24,%sp),%d2
	move.l sgl_Warp3DBase,%a6
	jsr (-210,%a6)
	movem.l (%sp),%d2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_SetStencilFunc,.-sgl_W3D_SetStencilFunc
	.section .text.sgl_W3D_AllocZBuffer,"ax",@progbits
	.globl sgl_W3D_AllocZBuffer
	.type sgl_W3D_AllocZBuffer,@function
sgl_W3D_AllocZBuffer:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-216,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_AllocZBuffer,.-sgl_W3D_AllocZBuffer
	.section .text.sgl_W3D_FreeZBuffer,"ax",@progbits
	.globl sgl_W3D_FreeZBuffer
	.type sgl_W3D_FreeZBuffer,@function
sgl_W3D_FreeZBuffer:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-222,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_FreeZBuffer,.-sgl_W3D_FreeZBuffer
	.section .text.sgl_W3D_ClearZBuffer,"ax",@progbits
	.globl sgl_W3D_ClearZBuffer
	.type sgl_W3D_ClearZBuffer,@function
sgl_W3D_ClearZBuffer:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-228,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_ClearZBuffer,.-sgl_W3D_ClearZBuffer
	.section .text.sgl_W3D_ReadZPixel,"ax",@progbits
	.globl sgl_W3D_ReadZPixel
	.type sgl_W3D_ReadZPixel,@function
sgl_W3D_ReadZPixel:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l (20,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-234,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_ReadZPixel,.-sgl_W3D_ReadZPixel
	.section .text.sgl_W3D_ReadZSpan,"ax",@progbits
	.globl sgl_W3D_ReadZSpan
	.type sgl_W3D_ReadZSpan,@function
sgl_W3D_ReadZSpan:
	lea (-8,%sp),%sp
	movem.l %d2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%d0
	move.l (20,%sp),%d1
	move.l (24,%sp),%d2
	move.l (28,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-240,%a6)
	movem.l (%sp),%d2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_ReadZSpan,.-sgl_W3D_ReadZSpan
	.section .text.sgl_W3D_SetZCompareMode,"ax",@progbits
	.globl sgl_W3D_SetZCompareMode
	.type sgl_W3D_SetZCompareMode,@function
sgl_W3D_SetZCompareMode:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-246,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetZCompareMode,.-sgl_W3D_SetZCompareMode
	.section .text.sgl_W3D_AllocStencilBuffer,"ax",@progbits
	.globl sgl_W3D_AllocStencilBuffer
	.type sgl_W3D_AllocStencilBuffer,@function
sgl_W3D_AllocStencilBuffer:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-252,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_AllocStencilBuffer,.-sgl_W3D_AllocStencilBuffer
	.section .text.sgl_W3D_ClearStencilBuffer,"ax",@progbits
	.globl sgl_W3D_ClearStencilBuffer
	.type sgl_W3D_ClearStencilBuffer,@function
sgl_W3D_ClearStencilBuffer:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-258,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_ClearStencilBuffer,.-sgl_W3D_ClearStencilBuffer
	.section .text.sgl_W3D_FillStencilBuffer,"ax",@progbits
	.globl sgl_W3D_FillStencilBuffer
	.type sgl_W3D_FillStencilBuffer,@function
sgl_W3D_FillStencilBuffer:
	lea (-16,%sp),%sp
	movem.l %d2/%d3/%d4/%a6,(%sp)
	move.l (20,%sp),%a0
	move.l (24,%sp),%d0
	move.l (28,%sp),%d1
	move.l (32,%sp),%d2
	move.l (36,%sp),%d3
	move.l (40,%sp),%d4
	move.l (44,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-264,%a6)
	movem.l (%sp),%d2/%d3/%d4/%a6
	lea (16,%sp),%sp
	rts
	.size sgl_W3D_FillStencilBuffer,.-sgl_W3D_FillStencilBuffer
	.section .text.sgl_W3D_FreeStencilBuffer,"ax",@progbits
	.globl sgl_W3D_FreeStencilBuffer
	.type sgl_W3D_FreeStencilBuffer,@function
sgl_W3D_FreeStencilBuffer:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-270,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_FreeStencilBuffer,.-sgl_W3D_FreeStencilBuffer
	.section .text.sgl_W3D_ReadStencilPixel,"ax",@progbits
	.globl sgl_W3D_ReadStencilPixel
	.type sgl_W3D_ReadStencilPixel,@function
sgl_W3D_ReadStencilPixel:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l (20,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-276,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_ReadStencilPixel,.-sgl_W3D_ReadStencilPixel
	.section .text.sgl_W3D_ReadStencilSpan,"ax",@progbits
	.globl sgl_W3D_ReadStencilSpan
	.type sgl_W3D_ReadStencilSpan,@function
sgl_W3D_ReadStencilSpan:
	lea (-8,%sp),%sp
	movem.l %d2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%d0
	move.l (20,%sp),%d1
	move.l (24,%sp),%d2
	move.l (28,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-282,%a6)
	movem.l (%sp),%d2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_ReadStencilSpan,.-sgl_W3D_ReadStencilSpan
	.section .text.sgl_W3D_SetLogicOp,"ax",@progbits
	.globl sgl_W3D_SetLogicOp
	.type sgl_W3D_SetLogicOp,@function
sgl_W3D_SetLogicOp:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-288,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetLogicOp,.-sgl_W3D_SetLogicOp
	.section .text.sgl_W3D_Hint,"ax",@progbits
	.globl sgl_W3D_Hint
	.type sgl_W3D_Hint,@function
sgl_W3D_Hint:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-294,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_Hint,.-sgl_W3D_Hint
	.section .text.sgl_W3D_SetDrawRegionWBM,"ax",@progbits
	.globl sgl_W3D_SetDrawRegionWBM
	.type sgl_W3D_SetDrawRegionWBM,@function
sgl_W3D_SetDrawRegionWBM:
	lea (-8,%sp),%sp
	movem.l %a2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%a1
	move.l (20,%sp),%a2
	move.l sgl_Warp3DBase,%a6
	jsr (-300,%a6)
	movem.l (%sp),%a2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_SetDrawRegionWBM,.-sgl_W3D_SetDrawRegionWBM
	.section .text.sgl_W3D_GetDriverState,"ax",@progbits
	.globl sgl_W3D_GetDriverState
	.type sgl_W3D_GetDriverState,@function
sgl_W3D_GetDriverState:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-306,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_GetDriverState,.-sgl_W3D_GetDriverState
	.section .text.sgl_W3D_Flush,"ax",@progbits
	.globl sgl_W3D_Flush
	.type sgl_W3D_Flush,@function
sgl_W3D_Flush:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-312,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_Flush,.-sgl_W3D_Flush
	.section .text.sgl_W3D_SetPenMask,"ax",@progbits
	.globl sgl_W3D_SetPenMask
	.type sgl_W3D_SetPenMask,@function
sgl_W3D_SetPenMask:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-318,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetPenMask,.-sgl_W3D_SetPenMask
	.section .text.sgl_W3D_SetStencilOp,"ax",@progbits
	.globl sgl_W3D_SetStencilOp
	.type sgl_W3D_SetStencilOp,@function
sgl_W3D_SetStencilOp:
	lea (-8,%sp),%sp
	movem.l %d2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%d0
	move.l (20,%sp),%d1
	move.l (24,%sp),%d2
	move.l sgl_Warp3DBase,%a6
	jsr (-324,%a6)
	movem.l (%sp),%d2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_SetStencilOp,.-sgl_W3D_SetStencilOp
	.section .text.sgl_W3D_SetWriteMask,"ax",@progbits
	.globl sgl_W3D_SetWriteMask
	.type sgl_W3D_SetWriteMask,@function
sgl_W3D_SetWriteMask:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-330,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetWriteMask,.-sgl_W3D_SetWriteMask
	.section .text.sgl_W3D_WriteStencilPixel,"ax",@progbits
	.globl sgl_W3D_WriteStencilPixel
	.type sgl_W3D_WriteStencilPixel,@function
sgl_W3D_WriteStencilPixel:
	lea (-8,%sp),%sp
	movem.l %d2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%d0
	move.l (20,%sp),%d1
	move.l (24,%sp),%d2
	move.l sgl_Warp3DBase,%a6
	jsr (-336,%a6)
	movem.l (%sp),%d2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_WriteStencilPixel,.-sgl_W3D_WriteStencilPixel
	.section .text.sgl_W3D_WriteStencilSpan,"ax",@progbits
	.globl sgl_W3D_WriteStencilSpan
	.type sgl_W3D_WriteStencilSpan,@function
sgl_W3D_WriteStencilSpan:
	lea (-12,%sp),%sp
	movem.l %d2/%a2/%a6,(%sp)
	move.l (16,%sp),%a0
	move.l (20,%sp),%d0
	move.l (24,%sp),%d1
	move.l (28,%sp),%d2
	move.l (32,%sp),%a1
	move.l (36,%sp),%a2
	move.l sgl_Warp3DBase,%a6
	jsr (-342,%a6)
	movem.l (%sp),%d2/%a2/%a6
	lea (12,%sp),%sp
	rts
	.size sgl_W3D_WriteStencilSpan,.-sgl_W3D_WriteStencilSpan
	.section .text.sgl_W3D_WriteZPixel,"ax",@progbits
	.globl sgl_W3D_WriteZPixel
	.type sgl_W3D_WriteZPixel,@function
sgl_W3D_WriteZPixel:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l (20,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-348,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_WriteZPixel,.-sgl_W3D_WriteZPixel
	.section .text.sgl_W3D_WriteZSpan,"ax",@progbits
	.globl sgl_W3D_WriteZSpan
	.type sgl_W3D_WriteZSpan,@function
sgl_W3D_WriteZSpan:
	lea (-12,%sp),%sp
	movem.l %d2/%a2/%a6,(%sp)
	move.l (16,%sp),%a0
	move.l (20,%sp),%d0
	move.l (24,%sp),%d1
	move.l (28,%sp),%d2
	move.l (32,%sp),%a1
	move.l (36,%sp),%a2
	move.l sgl_Warp3DBase,%a6
	jsr (-354,%a6)
	movem.l (%sp),%d2/%a2/%a6
	lea (12,%sp),%sp
	rts
	.size sgl_W3D_WriteZSpan,.-sgl_W3D_WriteZSpan
	.section .text.sgl_W3D_SetCurrentColor,"ax",@progbits
	.globl sgl_W3D_SetCurrentColor
	.type sgl_W3D_SetCurrentColor,@function
sgl_W3D_SetCurrentColor:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-360,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetCurrentColor,.-sgl_W3D_SetCurrentColor
	.section .text.sgl_W3D_SetCurrentPen,"ax",@progbits
	.globl sgl_W3D_SetCurrentPen
	.type sgl_W3D_SetCurrentPen,@function
sgl_W3D_SetCurrentPen:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-366,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetCurrentPen,.-sgl_W3D_SetCurrentPen
	.section .text.sgl_W3D_UpdateTexSubImage,"ax",@progbits
	.globl sgl_W3D_UpdateTexSubImage
	.type sgl_W3D_UpdateTexSubImage,@function
sgl_W3D_UpdateTexSubImage:
	lea (-16,%sp),%sp
	movem.l %a2/%a3/%a4/%a6,(%sp)
	move.l (20,%sp),%a0
	move.l (24,%sp),%a1
	move.l (28,%sp),%a2
	move.l (32,%sp),%d1
	move.l (36,%sp),%a3
	move.l (40,%sp),%a4
	move.l (44,%sp),%d0
	move.l sgl_Warp3DBase,%a6
	jsr (-372,%a6)
	movem.l (%sp),%a2/%a3/%a4/%a6
	lea (16,%sp),%sp
	rts
	.size sgl_W3D_UpdateTexSubImage,.-sgl_W3D_UpdateTexSubImage
	.section .text.sgl_W3D_FreeAllTexObj,"ax",@progbits
	.globl sgl_W3D_FreeAllTexObj
	.type sgl_W3D_FreeAllTexObj,@function
sgl_W3D_FreeAllTexObj:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-378,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_FreeAllTexObj,.-sgl_W3D_FreeAllTexObj
	.section .text.sgl_W3D_GetDestFmt,"ax",@progbits
	.globl sgl_W3D_GetDestFmt
	.type sgl_W3D_GetDestFmt,@function
sgl_W3D_GetDestFmt:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l sgl_Warp3DBase,%a6
	jsr (-384,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_GetDestFmt,.-sgl_W3D_GetDestFmt
	.section .text.sgl_W3D_DrawLineStrip,"ax",@progbits
	.globl sgl_W3D_DrawLineStrip
	.type sgl_W3D_DrawLineStrip,@function
sgl_W3D_DrawLineStrip:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-390,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_DrawLineStrip,.-sgl_W3D_DrawLineStrip
	.section .text.sgl_W3D_DrawLineLoop,"ax",@progbits
	.globl sgl_W3D_DrawLineLoop
	.type sgl_W3D_DrawLineLoop,@function
sgl_W3D_DrawLineLoop:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-396,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_DrawLineLoop,.-sgl_W3D_DrawLineLoop
	.section .text.sgl_W3D_GetDrivers,"ax",@progbits
	.globl sgl_W3D_GetDrivers
	.type sgl_W3D_GetDrivers,@function
sgl_W3D_GetDrivers:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l sgl_Warp3DBase,%a6
	jsr (-402,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_GetDrivers,.-sgl_W3D_GetDrivers
	.section .text.sgl_W3D_QueryDriver,"ax",@progbits
	.globl sgl_W3D_QueryDriver
	.type sgl_W3D_QueryDriver,@function
sgl_W3D_QueryDriver:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-408,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_QueryDriver,.-sgl_W3D_QueryDriver
	.section .text.sgl_W3D_GetDriverTexFmtInfo,"ax",@progbits
	.globl sgl_W3D_GetDriverTexFmtInfo
	.type sgl_W3D_GetDriverTexFmtInfo,@function
sgl_W3D_GetDriverTexFmtInfo:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l (16,%sp),%d1
	move.l sgl_Warp3DBase,%a6
	jsr (-414,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_GetDriverTexFmtInfo,.-sgl_W3D_GetDriverTexFmtInfo
	.section .text.sgl_W3D_RequestMode,"ax",@progbits
	.globl sgl_W3D_RequestMode
	.type sgl_W3D_RequestMode,@function
sgl_W3D_RequestMode:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-420,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_RequestMode,.-sgl_W3D_RequestMode
	.section .text.sgl_W3D_SetScissor,"ax",@progbits
	.globl sgl_W3D_SetScissor
	.type sgl_W3D_SetScissor,@function
sgl_W3D_SetScissor:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%a1
	move.l sgl_Warp3DBase,%a6
	jsr (-426,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_SetScissor,.-sgl_W3D_SetScissor
	.section .text.sgl_W3D_FlushFrame,"ax",@progbits
	.globl sgl_W3D_FlushFrame
	.type sgl_W3D_FlushFrame,@function
sgl_W3D_FlushFrame:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l sgl_Warp3DBase,%a6
	jsr (-432,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_FlushFrame,.-sgl_W3D_FlushFrame
	.section .text.sgl_W3D_TestMode,"ax",@progbits
	.globl sgl_W3D_TestMode
	.type sgl_W3D_TestMode,@function
sgl_W3D_TestMode:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%d0
	move.l sgl_Warp3DBase,%a6
	jsr (-438,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_TestMode,.-sgl_W3D_TestMode
	.section .text.sgl_W3D_SetChromaTestBounds,"ax",@progbits
	.globl sgl_W3D_SetChromaTestBounds
	.type sgl_W3D_SetChromaTestBounds,@function
sgl_W3D_SetChromaTestBounds:
	lea (-8,%sp),%sp
	movem.l %d2/%a6,(%sp)
	move.l (12,%sp),%a0
	move.l (16,%sp),%a1
	move.l (20,%sp),%d0
	move.l (24,%sp),%d1
	move.l (28,%sp),%d2
	move.l sgl_Warp3DBase,%a6
	jsr (-444,%a6)
	movem.l (%sp),%d2/%a6
	lea (8,%sp),%sp
	rts
	.size sgl_W3D_SetChromaTestBounds,.-sgl_W3D_SetChromaTestBounds
	.section .text.sgl_W3D_ClearDrawRegion,"ax",@progbits
	.globl sgl_W3D_ClearDrawRegion
	.type sgl_W3D_ClearDrawRegion,@function
sgl_W3D_ClearDrawRegion:
	lea (-4,%sp),%sp
	movem.l %a6,(%sp)
	move.l (8,%sp),%a0
	move.l (12,%sp),%d0
	move.l sgl_Warp3DBase,%a6
	jsr (-450,%a6)
	movem.l (%sp),%a6
	lea (4,%sp),%sp
	rts
	.size sgl_W3D_ClearDrawRegion,.-sgl_W3D_ClearDrawRegion
