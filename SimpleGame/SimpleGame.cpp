/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)
This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.
This program is distributed WITHOUT ANY WARRANTY.
*/
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "stdafx.h"
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include <windows.h>
#include <memory>
#include <iostream>
#include "Renderer.h"
#include "Game.h"
std::unique_ptr<Renderer> renderer;
Game game;
int lastTick=0;
void Display(){if(renderer){game.Render(*renderer);glutSwapBuffers();}}
void Tick(int){int now=glutGet(GLUT_ELAPSED_TIME);float dt=std::min(.05f,(now-lastTick)/1000.f);lastTick=now;game.Update(dt);glutPostRedisplay();glutTimerFunc(16,Tick,0);}
void KeyDown(unsigned char k,int,int){game.Key(k,true);}
void KeyUp(unsigned char k,int,int){game.Key(k,false);}
void Reshape(int w,int h){if(renderer)renderer->Resize(w,h);}
void Close(){renderer.reset();}
void WindowEntry(int state){if(state==GLUT_LEFT)game.ClearInput();}
int main(int argc,char**argv){
 glutInit(&argc,argv);glutInitContextVersion(3,3);glutInitContextProfile(GLUT_COMPATIBILITY_PROFILE);
 glutInitDisplayMode(GLUT_DOUBLE|GLUT_RGBA);glutInitWindowSize(1280,800);glutCreateWindow("HolyBloodPrototype");
 HWND window=FindWindowA(nullptr,"HolyBloodPrototype");if(window)SetWindowTextW(window,L"성혈의 잔향 - 마지막 이름");
 glewExperimental=GL_TRUE;if(glewInit()!=GLEW_OK||!GLEW_VERSION_3_3){std::cerr<<"OpenGL 3.3 compatibility support required.\n";return 1;}
 renderer.reset(new Renderer(1280,800));if(!renderer->IsInitialized()){std::cerr<<"Renderer initialization failed.\n";return 1;}
 // Prefer packaged data beside the executable; fall back to project working directories.
 wchar_t initial[MAX_PATH]={},exe[MAX_PATH]={};GetCurrentDirectoryW(MAX_PATH,initial);GetModuleFileNameW(nullptr,exe,MAX_PATH);std::wstring folder=exe;size_t slash=folder.find_last_of(L"\\/");if(slash!=std::wstring::npos)SetCurrentDirectoryW(folder.substr(0,slash).c_str());
 bool loaded=game.Load("Data/village.level");if(!loaded){SetCurrentDirectoryW(initial);loaded=game.Load("Data/village.level")||game.Load("SimpleGame/Data/village.level");}
 if(!loaded){std::cerr<<"Cannot load Data/village.level. Check project data deployment.\n";renderer.reset();return 1;}
 glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE,GLUT_ACTION_GLUTMAINLOOP_RETURNS);glutIgnoreKeyRepeat(1);
 glutDisplayFunc(Display);glutKeyboardFunc(KeyDown);glutKeyboardUpFunc(KeyUp);glutReshapeFunc(Reshape);glutCloseFunc(Close);glutEntryFunc(WindowEntry);
 lastTick=glutGet(GLUT_ELAPSED_TIME);glutTimerFunc(16,Tick,0);glutMainLoop();return 0;
}

