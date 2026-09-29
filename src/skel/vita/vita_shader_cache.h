#pragma once
// Kept separate from GL headers so librw's GL loader can include this safely.
bool VitaLoadShaderProgram(const char **vertex,const char **fragment,unsigned int *program);
void VitaSaveShaderProgram(const char **vertex,const char **fragment,unsigned int program);
