using System;

using System.Collections.Generic;

using System.Collections.Concurrent;

using System.Linq;

using System.Text;

using System.Text.RegularExpressions;

using System.IO;

using System.IO.MemoryMappedFiles;

using System.Runtime.InteropServices;

using System.Threading;

using System.Web.Script.Serialization;

using System.Windows.Forms;

using System.Drawing;



public class Binding { public string Phrase {get;set;} public int Skill {get;set;} }

public class Config { public string Model {get;set;} public int Microphone {get;set;} public double Confidence {get;set;} public List<Binding> Bindings {get;set;} }

public static class Rules {

    public static string Normalize(string s) {return Regex.Replace((s??"").Trim().ToLowerInvariant(), @"\s+", " ");}

    public static Dictionary<string,int> Validate(IEnumerable<Binding> bindings) {

        var result=new Dictionary<string,int>();

        foreach(var b in bindings) {

            string phrase=Normalize(b.Phrase); if(phrase.Length==0)continue;

            if(phrase.Length>120||phrase.Contains("[")||phrase.Contains("]")||phrase.Any(char.IsControl))throw new Exception("Use a phrase of 1-120 ordinary characters.");

            if(b.Skill<0||b.Skill>4095)throw new Exception("Choose an action for: "+phrase);

            if(result.ContainsKey(phrase))throw new Exception("Duplicate phrase: "+phrase);

            result.Add(phrase,b.Skill);

        }

        if(result.Count==0||result.Count>32)throw new Exception("Add between 1 and 32 phrase/action assignments.");

        return result;

    }

    public static int Match(string json,Dictionary<string,int> bindings,double threshold,out string text,out double confidence) {

        text="";confidence=0;

        var result=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(json);

        object t;if(!result.TryGetValue("text",out t))return -1;

        text=Normalize(t as string);int skill;

        if(text.Length==0||text.Contains("[unk]")||!bindings.TryGetValue(text,out skill))return -1;

        object words;if(!result.TryGetValue("result",out words))return -1;

        var array=words as System.Collections.IEnumerable;if(array==null)return -1;

        double total=0,min=1;int count=0;

        foreach(object item in array){var word=item as Dictionary<string,object>;object c;

            if(word==null||!word.TryGetValue("conf",out c))return -1;

            double v=Convert.ToDouble(c);if(double.IsNaN(v)||v<0||v>1)return -1;

            total+=v;min=Math.Min(min,v);++count;

        }

        if(count==0)return -1;confidence=total/count;

        return confidence>=threshold&&min>=.45?skill:-1;

    }

}

public static class Native {

    [DllImport("kernel32.dll")] public static extern ulong GetTickCount64();

    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] public static extern bool SetDllDirectory(string path);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern void vosk_set_log_level(int level);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern IntPtr vosk_model_new(byte[] path);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern void vosk_model_free(IntPtr model);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern int vosk_model_find_word(IntPtr model,byte[] word);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern IntPtr vosk_recognizer_new_grm(IntPtr model,float rate,byte[] grammar);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern void vosk_recognizer_set_words(IntPtr rec,int words);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern int vosk_recognizer_accept_waveform(IntPtr rec,byte[] data,int length);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern IntPtr vosk_recognizer_result(IntPtr rec);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern IntPtr vosk_recognizer_final_result(IntPtr rec);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern void vosk_recognizer_reset(IntPtr rec);

    [DllImport("libvosk.dll",CallingConvention=CallingConvention.Cdecl)] public static extern void vosk_recognizer_free(IntPtr rec);

    public static byte[] Utf8(string s){return Encoding.UTF8.GetBytes(s+"\0");}

    public static string Text(IntPtr p){if(p==IntPtr.Zero)return "{}";int n=0;while(n<1048576&&Marshal.ReadByte(p,n)!=0)++n;byte[] b=new byte[n];Marshal.Copy(p,b,0,n);return Encoding.UTF8.GetString(b);}

}

public class ActionChoice { public int Id {get;set;} public string Name {get;set;} }

// Offsets asserted against the game's C++ Shared layout in voice_bridge.h.

public sealed class GameBridge:IDisposable {

    MemoryMappedFile map;MemoryMappedViewAccessor view;Mutex mutex;

    public bool Ready, Connected;public ulong Session,GameAt,Ack;public string Status="Game not connected";

    public List<ActionChoice> Actions=new List<ActionChoice>();ulong sequence=Native.GetTickCount64()<<10;

    public GameBridge(string suffix=""){

        mutex=new Mutex(false,@"Local\D2RVRVoicePrototypeMutex1"+suffix);

        map=MemoryMappedFile.CreateOrOpen(@"Local\D2RVRVoicePrototype1"+suffix,14208);view=map.CreateViewAccessor(0,14208);

        Locked(delegate {if(view.ReadUInt32(0)!=0x564f4931){byte[] empty=new byte[14208];view.WriteArray(0,empty,0,empty.Length);view.Write(0,0x564f4931u);}view.Write(32,(byte)0);view.Write(14040,0UL);});

    }

    bool Locked(Action a){bool acquired=false;try{try{acquired=mutex.WaitOne(0);}catch(AbandonedMutexException){acquired=true;}if(!acquired)return false;a();return true;}finally{if(acquired)mutex.ReleaseMutex();}}

    string StringAt(long offset,int count){byte[] b=new byte[count];view.ReadArray(offset,b,0,count);int n=Array.IndexOf(b,(byte)0);return Encoding.UTF8.GetString(b,0,n<0?count:n);}

    public void Tick(bool enabled){Locked(delegate {

        ulong now=Native.GetTickCount64();view.Write(16,now);view.Write(32,(byte)(enabled?1:0));

        GameAt=view.ReadUInt64(8);Session=view.ReadUInt64(24);

        bool fresh=GameAt!=0&&now>=GameAt&&now-GameAt<500;Connected=fresh;

        Ready=fresh&&view.ReadByte(33)!=0;Ack=view.ReadUInt64(14072);Status=StringAt(14080,128);

        var list=new List<ActionChoice>();uint count=Math.Min(view.ReadUInt32(36),140u);

        if(GameAt!=0)for(int i=0;i<count;++i)list.Add(new ActionChoice{Id=view.ReadInt32(40+i*100),Name=StringAt(44+i*100,96)});

        if(list.Count>0)Actions=list;

    });}

    public bool Send(int skill,ulong session,ulong at){bool sent=false;Locked(delegate {

        ulong now=Native.GetTickCount64();

        if(view.ReadByte(32)==0||view.ReadByte(33)==0||session==0||session!=view.ReadUInt64(24)||now<at||now-at>400||now<view.ReadUInt64(8)||now-view.ReadUInt64(8)>500)return;

        view.Write(14048,at);view.Write(14056,session);view.Write(14064,skill);view.Write(14040,++sequence);sent=true;

    });return sent;}

    public void Dispose(){Locked(delegate {view.Write(32,(byte)0);view.Write(14040,0UL);});view.Dispose();map.Dispose();mutex.Dispose();}

}

public sealed class Microphone:IDisposable {

    [StructLayout(LayoutKind.Sequential,Pack=2)] struct Format {public ushort tag,channels;public uint rate,bytes;public ushort align,bits,extra;}

    [StructLayout(LayoutKind.Sequential)] struct Header {public IntPtr data;public uint length,recorded;public UIntPtr user;public uint flags,loops;public IntPtr next;public UIntPtr reserved;}

    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] public struct Caps {public ushort mid,pid;public uint version;[MarshalAs(UnmanagedType.ByValTStr,SizeConst=32)]public string name;public uint formats;public ushort channels,reserved;}

    [DllImport("winmm.dll")] public static extern uint waveInGetNumDevs();

    [DllImport("winmm.dll",CharSet=CharSet.Unicode,EntryPoint="waveInGetDevCapsW")] public static extern uint GetCaps(UIntPtr id,out Caps caps,uint size);

    [DllImport("winmm.dll")] static extern uint waveInOpen(out IntPtr h,uint id,ref Format f,IntPtr callback,IntPtr instance,uint flags);

    [DllImport("winmm.dll")] static extern uint waveInPrepareHeader(IntPtr h,IntPtr p,uint size);

    [DllImport("winmm.dll")] static extern uint waveInUnprepareHeader(IntPtr h,IntPtr p,uint size);

    [DllImport("winmm.dll")] static extern uint waveInAddBuffer(IntPtr h,IntPtr p,uint size);

    [DllImport("winmm.dll")] static extern uint waveInStart(IntPtr h);

    [DllImport("winmm.dll")] static extern uint waveInReset(IntPtr h);

    [DllImport("winmm.dll")] static extern uint waveInClose(IntPtr h);

    IntPtr device;IntPtr[] headers=new IntPtr[16],buffers=new IntPtr[16];bool[] prepared=new bool[16];int next;uint size=(uint)Marshal.SizeOf(typeof(Header));

    public Microphone(int id){try{

        var f=new Format{tag=1,channels=1,rate=16000,bytes=32000,align=2,bits=16};

        Check(waveInOpen(out device,unchecked((uint)id),ref f,IntPtr.Zero,IntPtr.Zero,0));

        for(int i=0;i<16;++i){buffers[i]=Marshal.AllocHGlobal(1600);headers[i]=Marshal.AllocHGlobal((int)size);

            Marshal.StructureToPtr(new Header{data=buffers[i],length=1600},headers[i],false);

            Check(waveInPrepareHeader(device,headers[i],size));prepared[i]=true;Check(waveInAddBuffer(device,headers[i],size));}

        Check(waveInStart(device));

    }catch{Dispose();throw;}}

    static void Check(uint e){if(e!=0)throw new Exception("Microphone error "+e+". Check microphone selection, Windows access and Virtual Desktop forwarding.");}

    public byte[] Read(){var h=(Header)Marshal.PtrToStructure(headers[next],typeof(Header));if((h.flags&1)==0)return null;

        byte[] b=new byte[Math.Min(h.recorded,1600u)];Marshal.Copy(buffers[next],b,0,b.Length);Check(waveInAddBuffer(device,headers[next],size));next=(next+1)%16;return b;}

    public void Dispose(){if(device!=IntPtr.Zero)waveInReset(device);for(int i=0;i<16;++i){if(prepared[i])waveInUnprepareHeader(device,headers[i],size);if(headers[i]!=IntPtr.Zero)Marshal.FreeHGlobal(headers[i]);if(buffers[i]!=IntPtr.Zero)Marshal.FreeHGlobal(buffers[i]);headers[i]=buffers[i]=IntPtr.Zero;prepared[i]=false;}if(device!=IntPtr.Zero)waveInClose(device);device=IntPtr.Zero;}

}

public sealed class SpeechEngine:IDisposable {

    IntPtr model,rec;

    public SpeechEngine(string path,Dictionary<string,int> phrases){try{

        Native.vosk_set_log_level(-1);model=Native.vosk_model_new(Native.Utf8(path));if(model==IntPtr.Zero)throw new Exception("Cannot load model folder. Run setup-voice-recognition.ps1 or choose a compatible Vosk model.");

        foreach(string word in phrases.Keys.SelectMany(p=>p.Split(' ')).Distinct())if(Native.vosk_model_find_word(model,Native.Utf8(word))<0)throw new Exception("The model does not recognize the word '"+word+"'. Use a spoken word in this model's language, not an invented spelling.");

        string grammar=new JavaScriptSerializer().Serialize(phrases.Keys.Concat(new[]{"[unk]"}).ToArray());

        rec=Native.vosk_recognizer_new_grm(model,16000,Native.Utf8(grammar));if(rec==IntPtr.Zero)throw new Exception("The model does not support command grammar.");Native.vosk_recognizer_set_words(rec,1);

    }catch{Dispose();throw;}}

    public string Feed(byte[] data){int result=Native.vosk_recognizer_accept_waveform(rec,data,data.Length);if(result<0)throw new Exception("Speech decoder error");return result==1?Native.Text(Native.vosk_recognizer_result(rec)):null;}

    public string Finish(){return Native.Text(Native.vosk_recognizer_final_result(rec));}

    public void Reset(){Native.vosk_recognizer_reset(rec);}

    public void Dispose(){if(rec!=IntPtr.Zero)Native.vosk_recognizer_free(rec);if(model!=IntPtr.Zero)Native.vosk_model_free(model);rec=model=IntPtr.Zero;}

}

public class Heard {public string Text;public int Skill;public double Confidence;public ulong At,Session;}

public sealed class VoiceWindow:Form {

    DataGridView grid=new DataGridView();ComboBox mic=new ComboBox();TextBox model=new TextBox();NumericUpDown confidence=new NumericUpDown();

    Button test=new Button(),live=new Button(),stop=new Button(),save=new Button(),refresh=new Button();TextBox log=new TextBox();Label status=new Label();

    TextBox phrase=new TextBox();ComboBox action=new ComboBox();Button add,remove;Label connection=new Label();string catalogKey="";
    ProgressBar meter=new ProgressBar();int audioLevel;

    GameBridge bridge;System.Windows.Forms.Timer timer=new System.Windows.Forms.Timer();Thread worker;volatile bool stopping,listening,gameReady;volatile bool liveMode;long currentSession;

    ConcurrentQueue<Heard> heard=new ConcurrentQueue<Heard>();ConcurrentQueue<string> messages=new ConcurrentQueue<string>();ulong lastSent,lastAck;bool closing;

    string configFile=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"D2R-PCVR","voice-recognition.json");

    public VoiceWindow(bool preview=false){

        if(!preview)bridge=new GameBridge();

        Text="D2R VR — Speech recognition";ClientSize=new Size(940,650);MinimumSize=new Size(850,600);Font=new Font("Segoe UI",10);StartPosition=FormStartPosition.CenterScreen;

        LabelAt("Type a phrase, choose a skill, then listen. No voice training. Recognition runs locally.",12,12,900);

        LabelAt("Microphone",12,48,110);mic.SetBounds(125,44,450,28);mic.DropDownStyle=ComboBoxStyle.DropDownList;Controls.Add(mic);mic.Items.Add("Windows default microphone");

        for(uint i=0;i<Microphone.waveInGetNumDevs();++i){Microphone.Caps caps;if(Microphone.GetCaps((UIntPtr)i,out caps,(uint)Marshal.SizeOf(typeof(Microphone.Caps)))==0)mic.Items.Add(caps.name);else mic.Items.Add("Device "+i);}mic.SelectedIndex=0;

        LabelAt("Model folder",12,84,110);model.SetBounds(125,80,690,28);model.Text=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"vosk-model-small-en-us-0.15");Controls.Add(model);

        var browse=ButtonAt("Browse",825,80,95,delegate {using(var d=new FolderBrowserDialog()){d.SelectedPath=model.Text;if(d.ShowDialog()==DialogResult.OK)model.Text=d.SelectedPath;}});

        LabelAt("Confidence",600,48,110);confidence.SetBounds(710,44,85,28);confidence.DecimalPlaces=2;confidence.Minimum=.5m;confidence.Maximum=1;confidence.Increment=.05m;confidence.Value=.75m;Controls.Add(confidence);

        LabelAt("Your phrase",12,120,110);phrase.SetBounds(125,116,285,28);Controls.Add(phrase);
        action.SetBounds(420,116,330,28);action.DropDownStyle=ComboBoxStyle.DropDownList;action.DisplayMember="Name";action.ValueMember="Id";Controls.Add(action);
        add=ButtonAt("Add / update",760,114,160,delegate {AddAssignment();});
        grid.SetBounds(12,156,908,224);grid.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;grid.AllowUserToAddRows=false;grid.AllowUserToDeleteRows=false;grid.ReadOnly=true;grid.SelectionMode=DataGridViewSelectionMode.FullRowSelect;grid.MultiSelect=false;grid.AutoSizeColumnsMode=DataGridViewAutoSizeColumnsMode.Fill;grid.RowHeadersWidth=30;

        grid.Columns.Add(new DataGridViewTextBoxColumn{Name="Phrase",HeaderText="Phrase to recognize"});

        grid.Columns.Add(new DataGridViewComboBoxColumn{Name="Skill",HeaderText="Skill / army command",DisplayMember="Name",ValueMember="Id",DataSource=new List<ActionChoice>{new ActionChoice{Id=-1,Name="Select an action"}}});

        grid.DataError+=(o,e)=>{e.ThrowException=false;};Controls.Add(grid);

        save=ButtonAt("Save assignments",12,390,170,delegate {try{SaveConfig();Append("Assignments saved.");}catch(Exception e){Append(e.Message);}});

        refresh=ButtonAt("Refresh skills",192,390,140,delegate {RefreshSkills(true);});
        remove=ButtonAt("Remove selected",342,390,170,delegate {try{if(grid.SelectedRows.Count>0){grid.Rows.Remove(grid.SelectedRows[0]);SaveConfig();}}catch(Exception e){Append(e.Message);}});
        grid.SelectionChanged+=(o,e)=>{if(grid.SelectedRows.Count>0){var r=grid.SelectedRows[0];phrase.Text=Convert.ToString(r.Cells[0].Value);if(r.Cells[1].Value!=null)action.SelectedValue=Convert.ToInt32(r.Cells[1].Value);}};
        connection.SetBounds(12,430,908,28);Controls.Add(connection);

        test=ButtonAt("Test recognition",12,464,180,delegate {Start(false);});live=ButtonAt("Start casting",202,464,170,delegate {Start(true);});stop=ButtonAt("Stop",382,464,100,delegate {Stop();});stop.Enabled=false;

        LabelAt("Mic level",505,469,85);meter.SetBounds(590,469,220,20);Controls.Add(meter);

        status.SetBounds(12,508,908,26);status.Anchor=AnchorStyles.Left|AnchorStyles.Right|AnchorStyles.Top;Controls.Add(status);

        log.SetBounds(12,540,908,95);log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;log.Anchor=AnchorStyles.Top|AnchorStyles.Bottom|AnchorStyles.Left|AnchorStyles.Right;Controls.Add(log);

        if(!preview){LoadConfig();RefreshSkills();timer.Interval=100;timer.Tick+=(o,e)=>Tick();timer.Start();}

        else {var choices=new List<ActionChoice>{new ActionChoice{Id=149,Name="Battle Orders"},new ActionChoice{Id=3906,Name="Army: Regroup"}};((DataGridViewComboBoxColumn)grid.Columns[1]).DataSource=choices;action.DataSource=choices.ToList();connection.Text="Game paused / headset inactive - cached skills remain selectable";grid.Rows.Add("my battle cry",149);grid.Rows.Add("fall back",3906);status.Text="Microphone off";}

        FormClosing+=(o,e)=>{if(worker!=null&&worker.IsAlive){closing=true;Stop();e.Cancel=true;}};

        FormClosed+=(o,e)=>{timer.Stop();if(bridge!=null)bridge.Dispose();};

    }

    void LabelAt(string text,int x,int y,int w){var c=new Label{Text=text};c.SetBounds(x,y,w,26);Controls.Add(c);}

    Button ButtonAt(string text,int x,int y,int w,Action action){var b=new Button{Text=text};b.SetBounds(x,y,w,32);b.Click+=(o,e)=>action();Controls.Add(b);return b;}

    void Append(string s){if(log.TextLength>12000)log.Clear();log.AppendText(DateTime.Now.ToString("HH:mm:ss")+"  "+s+Environment.NewLine);}

    List<Binding> Rows(){grid.EndEdit();var b=new List<Binding>();foreach(DataGridViewRow row in grid.Rows){if(row.IsNewRow)continue;b.Add(new Binding{Phrase=Convert.ToString(row.Cells[0].Value),Skill=row.Cells[1].Value==null?-1:Convert.ToInt32(row.Cells[1].Value)});}return b;}

    void SaveConfig(){var c=new Config{Model=model.Text,Microphone=mic.SelectedIndex-1,Confidence=(double)confidence.Value,Bindings=Rows()};if(c.Bindings.Count>0)Rules.Validate(c.Bindings);Directory.CreateDirectory(Path.GetDirectoryName(configFile));string temp=configFile+".tmp";File.WriteAllText(temp,new JavaScriptSerializer().Serialize(c),Encoding.UTF8);if(File.Exists(configFile))File.Replace(temp,configFile,null);else File.Move(temp,configFile);}

    void LoadConfig(){try{if(!File.Exists(configFile))return;var c=new JavaScriptSerializer().Deserialize<Config>(File.ReadAllText(configFile));if(c.Bindings==null||c.Bindings.Count>32)return;model.Text=c.Model;mic.SelectedIndex=Math.Max(0,Math.Min(mic.Items.Count-1,c.Microphone+1));confidence.Value=(decimal)Math.Max(.5,Math.Min(1,c.Confidence));

        var choices=new List<ActionChoice>{new ActionChoice{Id=-1,Name="Select an action"}};foreach(var b in c.Bindings)if(!choices.Any(a=>a.Id==b.Skill))choices.Add(new ActionChoice{Id=b.Skill,Name="Skill "+b.Skill});((DataGridViewComboBoxColumn)grid.Columns[1]).DataSource=choices;

        foreach(var b in c.Bindings)grid.Rows.Add(b.Phrase,b.Skill);

    }catch(Exception e){Append("Could not load assignments: "+e.Message);}}

    void AddAssignment(){try{
        var selected=action.SelectedItem as ActionChoice;if(selected==null||selected.Id<0)throw new Exception("Wait for your character's skills, then select an action.");
        string text=Rules.Normalize(phrase.Text);if(text.Length==0)throw new Exception("Type the words you want to say in Your phrase.");
        var rows=Rows();var existing=rows.Find(b=>Rules.Normalize(b.Phrase)==text);
        if(existing!=null)existing.Skill=selected.Id;else rows.Add(new Binding{Phrase=text,Skill=selected.Id});Rules.Validate(rows);
        grid.Rows.Clear();foreach(var row in rows)grid.Rows.Add(row.Phrase,row.Skill);SaveConfig();Append("Saved: "+text+" -> "+selected.Name);
    }catch(Exception e){Append(e.Message);}}
    void RefreshSkills(bool announce=false){
        var choices=new List<ActionChoice>{new ActionChoice{Id=-1,Name="Select an action"}};choices.AddRange(bridge.Actions.GroupBy(a=>a.Id).Select(g=>g.First()));foreach(var b in Rows())if(!choices.Any(a=>a.Id==b.Skill))choices.Add(new ActionChoice{Id=b.Skill,Name="Skill "+b.Skill+" (not currently available)"});
        int selected=action.SelectedValue is int?(int)action.SelectedValue:-1;
        ((DataGridViewComboBoxColumn)grid.Columns[1]).DataSource=choices;action.DataSource=choices.ToList();action.SelectedValue=selected;
        catalogKey=string.Join("|",bridge.Actions.Select(a=>a.Id+":"+a.Name));
        if(announce)Append(bridge.Actions.Count>0?"Character skills loaded; this list stays available with the headset off.":"Waiting for character skills. Enter an offline game and briefly wear the headset; skills refresh automatically.");
    }
    void Start(bool cast){try{

        if(worker!=null&&worker.IsAlive)return;var bindings=Rules.Validate(Rows());if(!Directory.Exists(model.Text))throw new Exception("Choose a downloaded model folder first.");SaveConfig();string path=model.Text;int device=mic.SelectedIndex-1;double threshold=(double)confidence.Value;

        stopping=false;listening=false;liveMode=cast;lastSent=0;heard=new ConcurrentQueue<Heard>();phrase.Enabled=action.Enabled=add.Enabled=remove.Enabled=grid.Enabled=mic.Enabled=model.Enabled=confidence.Enabled=save.Enabled=refresh.Enabled=test.Enabled=live.Enabled=false;stop.Enabled=true;

        worker=new Thread(delegate(){try{

            messages.Enqueue("Loading speech model…");using(var engine=new SpeechEngine(path,bindings))using(var audio=new Microphone(device)){

                listening=true;messages.Enqueue(cast?"Listening for assigned phrases. Focus D2R to cast.":"Listening — recognition only; no casts.");ulong context=0;bool wasAllowed=false;ulong lastAudio=Native.GetTickCount64();

                while(!stopping){byte[] bytes=audio.Read();if(bytes==null){if(Native.GetTickCount64()-lastAudio>3000)throw new Exception("Microphone stopped delivering audio. Check the selected device and reconnect it.");Thread.Sleep(5);continue;}

                    lastAudio=Native.GetTickCount64();double energy=0;for(int n=0;n+1<bytes.Length;n+=2){short sample=(short)(bytes[n]|(bytes[n+1]<<8));energy+=(double)sample*sample;}

                    Interlocked.Exchange(ref audioLevel,Math.Min(100,(int)(Math.Sqrt(energy/Math.Max(1,bytes.Length/2))/32768*400)));



                    ulong session=unchecked((ulong)Interlocked.Read(ref currentSession));bool allowed=!cast||gameReady;

                    if(!allowed){if(wasAllowed)engine.Reset();wasAllowed=false;context=0;continue;}

                    if(!wasAllowed||context!=session){engine.Reset();context=session;wasAllowed=true;}

                    string result=engine.Feed(bytes);if(result==null)continue;string text;double score;int skill=Rules.Match(result,bindings,threshold,out text,out score);

                    if(text.Length>0&&!stopping&&(!cast||gameReady&&context==unchecked((ulong)Interlocked.Read(ref currentSession))))heard.Enqueue(new Heard{Text=text,Skill=skill,Confidence=score,At=Native.GetTickCount64(),Session=context});

                }

            }

        }catch(Exception e){messages.Enqueue("Recognition stopped: "+e.Message);}finally{listening=false;}});worker.IsBackground=true;worker.Start();

    }catch(Exception e){Append(e.Message);}}

    void Stop(){stopping=true;listening=false;liveMode=false;bridge.Tick(false);}

    void Tick(){bridge.Tick(liveMode&&listening&&!stopping);
        string key=string.Join("|",bridge.Actions.Select(a=>a.Id+":"+a.Name));if(key!=catalogKey)RefreshSkills();
        connection.Text=bridge.Connected?(bridge.Ready?"Game connected - ready to cast":"Game connected - casting paused (menus / busy controls)"):(bridge.GameAt!=0?"Game paused / headset inactive - cached skills remain selectable":"Waiting for VR game - enter an offline game and wear the headset to load skills");gameReady=bridge.Ready;Interlocked.Exchange(ref currentSession,unchecked((long)bridge.Session));

        ulong now=Native.GetTickCount64();meter.Value=listening?Interlocked.CompareExchange(ref audioLevel,0,0):0;status.Text=listening?(liveMode?(bridge.Ready?"Listening — casts enabled":"Listening — casting blocked (focus D2R, close menus, free controls)"):"Listening — test mode"):(worker!=null&&worker.IsAlive?"Starting / stopping recognition…":"Microphone off");

        string message;while(messages.TryDequeue(out message))Append(message);Heard h;

        while(heard.TryDequeue(out h)){Append("Heard: “"+h.Text+"”"+(h.Skill>=0?" ("+h.Confidence.ToString("P0")+")":" — not an assigned/confident phrase"));if(liveMode&&listening&&!stopping&&h.Skill>=0&&now>=h.At&&now-h.At<=400&&(lastSent==0||now-lastSent>=1500)){if(bridge.Send(h.Skill,h.Session,h.At))lastSent=now;else Append("Command dropped: game not ready.");}}

        if(bridge.Ack!=0&&bridge.Ack!=lastAck){lastAck=bridge.Ack;Append("Game: "+bridge.Status);}

        if(worker!=null&&!worker.IsAlive){worker=null;liveMode=false;phrase.Enabled=action.Enabled=add.Enabled=remove.Enabled=grid.Enabled=mic.Enabled=model.Enabled=confidence.Enabled=save.Enabled=refresh.Enabled=test.Enabled=live.Enabled=true;stop.Enabled=false;bridge.Tick(false);if(closing)Close();}

    }

}

public static class Program {

    [STAThread] public static int Main(string[] args){

        Native.SetDllDirectory(AppDomain.CurrentDomain.BaseDirectory);

        if(args.Length>1&&args[0]=="--ui-preview"){

            Application.EnableVisualStyles();using(var f=new VoiceWindow(true)){f.Show();Application.DoEvents();using(var bmp=new Bitmap(f.Width,f.Height)){f.DrawToBitmap(bmp,new Rectangle(0,0,f.Width,f.Height));bmp.Save(args[1]);}f.Close();}return 0;

        }

        if(args.Length==2&&args[0]=="--embedded")return Embedded.Run(int.Parse(args[1]));
        if(args.Length>0&&args[0]=="--test")return SelfTest();

        if(args.Length>0&&args[0]=="--transcribe")return Transcribe(args);

        bool created;using(var single=new Mutex(true,@"Local\D2RVRVoiceTrainerSingleton1",out created)){

            if(!created){MessageBox.Show("Another voice helper is open. Close it first.");return 1;}

            try{Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new VoiceWindow());return 0;}

            catch(Exception e){MessageBox.Show(e.ToString(),"Voice recognition error");return 1;}

        }

    }

    static void Assert(bool value){if(!value)throw new Exception("Test failed");}

    static int SelfTest(){try{

        var b=Rules.Validate(new[]{new Binding{Phrase="  Battle   Orders ",Skill=149},new Binding{Phrase="attack",Skill=3904}});

        Assert(b["battle orders"]==149);bool duplicate=false;try{Rules.Validate(new[]{new Binding{Phrase="Attack",Skill=1},new Binding{Phrase="attack",Skill=2}});}catch{duplicate=true;}Assert(duplicate);

        string t;double c;

        Assert(Rules.Match("{\"text\":\"battle orders\",\"result\":[{\"conf\":0.95},{\"conf\":0.92}]}",b,.75,out t,out c)==149);

        Assert(Rules.Match("{\"text\":\"attack\",\"result\":[{\"conf\":0.3}]}",b,.75,out t,out c)==-1);

        Assert(Rules.Match("{\"text\":\"please attack\",\"result\":[{\"conf\":1}]}",b,.75,out t,out c)==-1);

        Assert(Rules.Match("{\"text\":\"[unk]\"}",b,.75,out t,out c)==-1);

        string suffix="-test-"+Guid.NewGuid().ToString("N");
        using(var bridge=new GameBridge(suffix))using(var map=MemoryMappedFile.OpenExisting(@"Local\D2RVRVoicePrototype1"+suffix))using(var view=map.CreateViewAccessor()){
            ulong now=Native.GetTickCount64();view.Write(8,now-2000);view.Write(24,17UL);view.Write(33,(byte)1);view.Write(36,1u);view.Write(40,149);
            byte[] name=Encoding.UTF8.GetBytes("Battle Orders\0");view.WriteArray(44,name,0,name.Length);
            bridge.Tick(false);Assert(!bridge.Ready&&!bridge.Connected&&bridge.Actions.Count==1&&bridge.Actions[0].Id==149);
            view.Write(36,0u);bridge.Tick(false);Assert(bridge.Actions.Count==1); // Headset-off must not erase choices.
            view.Write(8,Native.GetTickCount64());bridge.Tick(true);Assert(bridge.Ready&&bridge.Connected);
            Assert(!bridge.Send(149,18,Native.GetTickCount64())); // Wrong character session.
            Assert(bridge.Send(149,17,Native.GetTickCount64()));Assert(view.ReadInt32(14064)==149&&view.ReadUInt64(14040)!=0);
            view.Write(8,Native.GetTickCount64()-2000);Assert(!bridge.Send(149,17,Native.GetTickCount64())); // No deferred casts.
        }
        Console.WriteLine("Phrase recognition, cached skill catalogue, IPC dispatch and stale/session rejection passed");return 0;

    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}

    static int Transcribe(string[] args){try{

        if(args.Length<4)throw new Exception("--transcribe MODEL PCM16LE_FILE phrase [phrase ...]");

        var b=new Dictionary<string,int>();for(int i=3;i<args.Length;++i)b.Add(Rules.Normalize(args[i]),i);

        byte[] pcm=File.ReadAllBytes(args[2]);using(var e=new SpeechEngine(args[1],b)){

            for(int i=0;i<pcm.Length;i+=3200){var chunk=new byte[Math.Min(3200,pcm.Length-i)];Buffer.BlockCopy(pcm,i,chunk,0,chunk.Length);string r=e.Feed(chunk);if(r!=null)Console.WriteLine(r);}Console.WriteLine(e.Finish());}return 0;

    }catch(Exception e){Console.Error.WriteLine(e);return 1;}}

}


// Launched and configured by the game's Voice page. No form, console, manual
// helper launch, or desktop configuration is involved in this mode.
public static class Embedded {
    public static int Run(int pid){
        using(var parent=System.Diagnostics.Process.GetProcessById(pid))
        using(var map=MemoryMappedFile.OpenExisting(@"Local\D2RVRVoiceIntegrated"+pid))
        using(var view=map.CreateViewAccessor(0,8192))
        using(var mutex=Mutex.OpenExisting(@"Local\D2RVRVoiceIntegrated"+pid+"Mutex"))
        using(var bridge=new GameBridge("-game-"+pid)){
            Action<Action> locked=delegate(Action action){bool held=false;try{try{held=mutex.WaitOne(0);}catch(AbandonedMutexException){held=true;}if(held)action();}finally{if(held)mutex.ReleaseMutex();}};
            Action<string> report=delegate(string text){byte[] bytes=Encoding.UTF8.GetBytes(text);locked(delegate{byte[] line=new byte[240];Buffer.BlockCopy(bytes,0,line,0,Math.Min(239,bytes.Length));view.WriteArray(16,line,0,line.Length);});};
            uint generation=0,mode=0;SpeechEngine engine=null;Microphone microphone=null;Config config=null;Dictionary<string,int> phrases=null;
            ulong lastSent=0,lastAudio=0,session=0,lastAck=0;bool allowedBefore=false;string lastState="";
            try{while(!parent.HasExited){
                uint incoming=0,nextMode=0;bool exit=false;string json=null;
                locked(delegate{exit=view.ReadUInt32(12)!=0;incoming=view.ReadUInt32(0);nextMode=view.ReadUInt32(4);uint n=view.ReadUInt32(8);if(incoming!=generation&&n>0&&n<7936){byte[] data=new byte[n];view.ReadArray(256,data,0,data.Length);json=Encoding.UTF8.GetString(data);}});
                if(exit)break;
                if(incoming!=0&&incoming!=generation&&json!=null){
                    generation=incoming;mode=nextMode;bridge.Tick(false);if(microphone!=null)microphone.Dispose();if(engine!=null)engine.Dispose();microphone=null;engine=null;allowedBefore=false;lastState="";
                    if(mode==0){report("Voice recognition off");}
                    else try{
                        config=new JavaScriptSerializer().Deserialize<Config>(json);phrases=Rules.Validate(config.Bindings);
                        report("Loading local speech model...");engine=new SpeechEngine(config.Model,phrases);microphone=new Microphone(config.Microphone);lastAudio=Native.GetTickCount64();
                        report(mode==1?"Test listening - say an assigned phrase":"Listening - close settings to cast");
                    }catch(Exception e){if(microphone!=null)microphone.Dispose();if(engine!=null)engine.Dispose();microphone=null;engine=null;report(e.Message);}
                }
                bridge.Tick(mode==2&&engine!=null&&microphone!=null);
                if(engine==null||microphone==null){Thread.Sleep(100);continue;}
                try{
                    byte[] bytes=microphone.Read();if(bytes==null){if(Native.GetTickCount64()-lastAudio>3000)throw new Exception("No microphone audio. Check selected device / Virtual Desktop forwarding.");Thread.Sleep(5);continue;}lastAudio=Native.GetTickCount64();
                    bool allowed=mode==1||bridge.Ready;
                    string state=mode==1?"Test listening - no casts":allowed?"Listening - casting enabled":"Listening paused - close menus and free controls";
                    if(state!=lastState){lastState=state;report(state);}
                    if(!allowed){if(allowedBefore)engine.Reset();allowedBefore=false;continue;}
                    if(!allowedBefore||session!=bridge.Session){engine.Reset();session=bridge.Session;allowedBefore=true;}
                    string result=engine.Feed(bytes);
                    if(result!=null){string text;double score;int skill=Rules.Match(result,phrases,config.Confidence,out text,out score);
                        if(text.Length>0){string message="Heard: "+text;
                            if(skill<0)message+=" (not matched)";
                            else if(mode==1)message+=" (matched, test only)";
                            else {ulong now=Native.GetTickCount64();if(lastSent==0||now-lastSent>=1500){if(bridge.Send(skill,session,now)){lastSent=now;message+=" - sent";}else message+=" - game not ready";}else message+=" - cooldown";}
                            report(message);
                        }
                    }
                    if(bridge.Ack!=0&&bridge.Ack!=lastAck){lastAck=bridge.Ack;report(bridge.Status);}
                }catch(Exception e){report(e.Message);microphone.Dispose();engine.Dispose();microphone=null;engine=null;bridge.Tick(false);}
            }}finally{if(microphone!=null)microphone.Dispose();if(engine!=null)engine.Dispose();}
        }
        return 0;
    }
}
