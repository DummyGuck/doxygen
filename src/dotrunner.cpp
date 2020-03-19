/******************************************************************************
*
* Copyright (C) 1997-2019 by Dimitri van Heesch.
*
* Permission to use, copy, modify, and distribute this software and its
* documentation under the terms of the GNU General Public License is hereby
* granted. No representations are made about the suitability of this software
* for any purpose. It is provided "as is" without express or implied warranty.
* See the GNU General Public License for more details.
*
* Documents produced by Doxygen are derivative works derived from the
* input used in their production; they are not affected by this license.
*
*/

#include "dotrunner.h"

#include "util.h"
#include "portable.h"
#include "dot.h"
#include "message.h"
#include "ftextstream.h"
#include "config.h"

// the graphicx LaTeX has a limitation of maximum size of 16384
// To be on the save side we take it a little bit smaller i.e. 150 inch * 72 dpi
// It is anyway hard to view these size of images
#define MAX_LATEX_GRAPH_INCH  150
#define MAX_LATEX_GRAPH_SIZE  (MAX_LATEX_GRAPH_INCH * 72)

//-----------------------------------------------------------------------------------------

// since dot silently reproduces the input file when it does not
// support the PNG format, we need to check the result.
static void checkPngResult(const char *imgName)
{
  FILE *f = Portable::fopen(imgName,"rb");
  if (f)
  {
    char data[4];
    if (fread(data,1,4,f)==4)
    {
      if (!(data[1]=='P' && data[2]=='N' && data[3]=='G'))
      {
        err("Image '%s' produced by dot is not a valid PNG!\n"
          "You should either select a different format "
          "(DOT_IMAGE_FORMAT in the config file) or install a more "
          "recent version of graphviz (1.7+)\n",imgName
        );
      }
    }
    else
    {
      err("Could not read image '%s' generated by dot!\n",imgName);
    }
    fclose(f);
  }
  else
  {
    err("Could not open image '%s' generated by dot!\n",imgName);
  }
}

static bool resetPDFSize(const int width,const int height, const char *base)
{
  QCString tmpName = QCString(base)+".tmp";
  QCString patchFile = QCString(base)+".dot";
  if (!QDir::current().rename(patchFile,tmpName))
  {
    err("Failed to rename file %s to %s!\n",patchFile.data(),tmpName.data());
    return FALSE;
  }
  QFile fi(tmpName);
  QFile fo(patchFile);
  if (!fi.open(IO_ReadOnly)) 
  {
    err("problem opening file %s for patching!\n",tmpName.data());
    QDir::current().rename(tmpName,patchFile);
    return FALSE;
  }
  if (!fo.open(IO_WriteOnly))
  {
    err("problem opening file %s for patching!\n",patchFile.data());
    QDir::current().rename(tmpName,patchFile);
    fi.close();
    return FALSE;
  }
  FTextStream t(&fo);
  const int maxLineLen=100*1024;
  while (!fi.atEnd()) // foreach line
  {
    QCString line(maxLineLen);
    int numBytes = fi.readLine(line.rawData(),maxLineLen);
    if (numBytes<=0)
    {
      break;
    }
    line.resize(numBytes+1);
    if (line.find("LATEX_PDF_SIZE") != -1)
    {
      double scale = (width > height ? width : height)/double(MAX_LATEX_GRAPH_INCH);
      t << "  size=\""<<width/scale << "," <<height/scale <<"\";\n";
    }
    else
      t << line;
  }
  fi.close();
  fo.close();
  // remove temporary file
  QDir::current().remove(tmpName);
  return TRUE;
}

bool DotRunner::readBoundingBox(const char *fileName,int *width,int *height,bool isEps)
{
  const char *bb = isEps ? "%%PageBoundingBox:" : "/MediaBox [";
  int bblen = (int)strlen(bb);
  FILE *f = Portable::fopen(fileName,"rb");
  if (!f) 
  {
    //printf("readBoundingBox: could not open %s\n",fileName);
    return FALSE;
  }
  const int maxLineLen=1024;
  char buf[maxLineLen];
  while (fgets(buf,maxLineLen,f)!=NULL)
  {
     const char *p = strstr(buf,bb);
     if (p) // found PageBoundingBox or /MediaBox string
     {
       int x,y;
       fclose(f);
       if (sscanf(p+bblen,"%d %d %d %d",&x,&y,width,height)!=4)
       {
         //printf("readBoundingBox sscanf fail\n");
         return FALSE;
       }
       return TRUE;
     }
  }
  err("Failed to extract bounding box from generated diagram file %s\n",fileName);
  fclose(f);
  return FALSE;
}

//---------------------------------------------------------------------------------

DotRunner::DotRunner(const QCString& absDotName, const QCString& md5Hash)
  : m_file(absDotName)
  , m_md5Hash(md5Hash)
  , m_dotExe(Config_getString(DOT_PATH)+"dot")
  , m_cleanUp(Config_getBool(DOT_CLEANUP))
{
}

void DotRunner::addJob(const char *format,const char *output)
{
    
  for (auto& s: m_jobs)
  {
    if (qstrcmp(s.format.data(), format) != 0) continue;
    if (qstrcmp(s.output.data(), output) != 0) continue;
    // we have this job already
    return;
  }
  QCString args = QCString("-T")+format+" -o \""+output+"\"";
  m_jobs.emplace_back(format, output, args);
}

QCString getBaseNameOfOutput(QCString const& output)
{
  int index = output.findRev('.');
  if (index < 0) return output;
  return output.left(index);
}

bool DotRunner::run()
{
  int exitCode=0;

  QCString dotArgs;

  // create output
  if (Config_getBool(DOT_MULTI_TARGETS))
  {
    dotArgs=QCString("\"")+m_file.data()+"\"";
    for (auto& s: m_jobs)
    {
      dotArgs+=' ';
      dotArgs+=s.args.data();
    }
    if ((exitCode=Portable::system(m_dotExe.data(),dotArgs,FALSE))!=0) goto error;
  }
  else
  {
    for (auto& s : m_jobs)
    {
      dotArgs=QCString("\"")+m_file.data()+"\" "+s.args.data();
      if ((exitCode=Portable::system(m_dotExe.data(),dotArgs,FALSE))!=0) goto error;
    }
  }

  // check output
  // As there should be only one pdf file be generated, we don't need code for regenerating multiple pdf files in one call
  for (auto& s : m_jobs)
  {
    if (qstrncmp(s.format.data(), "pdf", 3) == 0)
    {
      int width=0,height=0;
      if (!readBoundingBox(s.output.data(),&width,&height,FALSE)) goto error;
      if ((width > MAX_LATEX_GRAPH_SIZE) || (height > MAX_LATEX_GRAPH_SIZE))
      {
        if (!resetPDFSize(width,height,getBaseNameOfOutput(s.output.data()))) goto error;
        dotArgs=QCString("\"")+m_file.data()+"\" "+s.args.data();
        if ((exitCode=Portable::system(m_dotExe.data(),dotArgs,FALSE))!=0) goto error;
      }
    }

    if (qstrncmp(s.format.data(), "png", 3) == 0)
    {
      checkPngResult(s.output.data());
    }
  }

  // remove .dot files
  if (m_cleanUp) 
  {
    //printf("removing dot file %s\n",m_file.data());
    Portable::unlink(m_file.data());
  }

  // create checksum file
  if (!m_md5Hash.isEmpty()) 
  {
    QCString md5Name = getBaseNameOfOutput(m_file.data()) + ".md5";
    FILE *f = Portable::fopen(md5Name,"w");
    if (f)
    {
      fwrite(m_md5Hash.data(),1,32,f); 
      fclose(f);
    }
  }
  return TRUE;
error:
  err("Problems running dot: exit code=%d, command='%s', arguments='%s'\n",
    exitCode,m_dotExe.data(),dotArgs.data());
  return FALSE;
}


//--------------------------------------------------------------------

void DotRunnerQueue::enqueue(DotRunner *runner)
{
  std::lock_guard<std::mutex> locker(m_mutex);
  m_queue.push(runner);
  m_bufferNotEmpty.notify_all();
}

DotRunner *DotRunnerQueue::dequeue()
{
  std::unique_lock<std::mutex> locker(m_mutex);
  
   // wait until something is added to the queue
   m_bufferNotEmpty.wait(locker, [this]() {return !m_queue.empty(); });
  
  DotRunner *result = m_queue.front();
  m_queue.pop();
  return result;
}

uint DotRunnerQueue::count() const
{
  std::lock_guard<std::mutex> locker(m_mutex);
  return m_queue.size();
}

//--------------------------------------------------------------------

DotWorkerThread::DotWorkerThread(DotRunnerQueue *queue)
  : m_queue(queue)
{
}

void DotWorkerThread::run()
{
  DotRunner *runner;
  while ((runner=m_queue->dequeue()))
  {
    runner->run();
  }
}
