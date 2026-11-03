(function(root) {
  "use strict";
  // Preserve DriveDemo's existing public convention: pitch is positive NOSE UP.
  // Firmware body is +X forward,+Y left,+Z up; TEL pitch is positive NOSE DOWN.
  // The detailed asset is +Z forward,+X left,+Y up. The fixed 42-degree display
  // angle is a camera presentation offset, never added to the heading readout.
  function toFrame(packet) {
    return { source: packet.mode === "REPLAY" ? "replay" : "uart",
      roll: packet.roll, pitch: -packet.pitch, heading: packet.yaw,
      temperature: packet.temp, humidity: packet.rh, pressure: packet.hpa,
      lux: packet.lux, distance: packet.range_mm >= 0 ? packet.range_mm / 1000 : null,
      distanceState: packet.range_state, poseValid: !!packet.imu && !packet.stale && packet.connected,
      mag: !!packet.mag, stale: packet.stale, connected: packet.connected,
      bodyRates: [packet.rr, packet.pr, packet.yr] };
  }
  function toCompass(packet) {
    // R9 user-selected relative display: consume the exact integer TEL heading,
    // not the smoothed model or the independent magnetic calibration.
    if(packet.heading_mode === "REL") {
      const ready=!!packet.connected&&!packet.stale&&!!packet.imu&&Number.isFinite(packet.yaw);
      const heading=ready?Math.round((packet.yaw%360+360)%360)%360:null;
      return {relative:true,source:"REL",valid:ready,heading_x10:heading===null?null:heading*10,
        status:!packet.connected?"OFFLINE":packet.stale?"STALE":ready?"RELATIVE HEADING":"WAIT IMU",
        message:ready?"相对方向：与板载屏幕使用同一份数据和整数角度":"等待有效板卡姿态数据",
        selftest_result:0,selftest_status:0};
    }
    const c=packet.compass||{}, recent=!!packet.connected&&!c.stale&&c.age_ms!=null;
    const selftest=Number.isInteger(c.selftest_result)?c.selftest_result:0;
    const selftestErrors={2:["SELFTEST FAILED","磁力计自检未通过；请检查磁干扰、供电与器件"],
      3:["SELFTEST INCONCLUSIVE","磁力计自检结果不确定，暂不提供磁北角"],
      4:["SELFTEST IO ERROR","磁力计自检通信错误；请检查连接"],
      5:["SELFTEST TIMEOUT","磁力计自检超时，暂不提供磁北角"]};
    const candidate=recent&&(selftest===0||selftest===1)&&!!c.detected&&!!c.fresh&&!!c.calibrated&&!!c.aligned&&!!c.level&&!c.collecting;
    let status="NOT READY",message="板端指南针暂不可用";
    if(!packet.connected){status="OFFLINE";message="请连接真实板卡";}
    else if(c.age_ms==null){status="WAITING CMP";message="等待独立磁指南针 CMP 数据";}
    else if(c.stale){status="STALE";message="磁力计数据已过期";}
    else if(selftestErrors[selftest]){[status,message]=selftestErrors[selftest];}
    else if(!c.fresh){status="STALE";message="磁力计数据已过期";}
    else if(!c.detected){status="NO SENSOR";message="未检测到磁力计";}
    else if(c.collecting){status="CALIBRATING";message="保持平放，缓慢完整旋转板卡";}
    else if(!c.level){status="KEEP LEVEL";message="请将板卡平放；二维指南针不作倾斜补偿";}
    else if(!c.calibrated){status="CAL REQUIRED";message="需要磁场校准";}
    else if(!c.aligned){status="ALIGN REQUIRED";message="需要按实际前向设置参考磁北角";}
    else if(!c.direction_verified){status="VERIFY DIRECTION";message="当前为待核对读数；先核对顺时针与多个方向";}
    else if(c.valid){status="MAGNETIC NORTH";message="与板端同源磁北角；保持平放";}
    return {...c,selftest_result:selftest,selftest_status:Number.isInteger(c.selftest_status)?c.selftest_status:0,
      valid:!!(candidate&&c.valid&&c.direction_verified),
      heading_x10:candidate&&Number.isInteger(c.heading_x10)?c.heading_x10:null,status,message};
  }
  function detailedQuaternion(roll, noseUpPitch, heading) {
    const h=Math.PI/360, a=(42-heading)*h, b=-noseUpPitch*h, c=roll*h;
    const sa=Math.sin(a),ca=Math.cos(a),sb=Math.sin(b),cb=Math.cos(b),sc=Math.sin(c),cc=Math.cos(c);
    // qY(42-heading) * qX(-noseUpPitch) * qZ(roll)
    return [ca*sb*cc+sa*cb*sc, sa*cb*cc-ca*sb*sc,
            ca*cb*sc-sa*sb*cc, ca*cb*cc+sa*sb*sc];
  }
  function rotate(q,v) {
    const [x,y,z,w]=q,[vx,vy,vz]=v;
    const tx=2*(y*vz-z*vy),ty=2*(z*vx-x*vz),tz=2*(x*vy-y*vx);
    return [vx+w*tx+y*tz-z*ty,vy+w*ty+z*tx-x*tz,vz+w*tz+x*ty-y*tx];
  }
  function fallbackQuaternion(roll,noseUpPitch,heading) {
    return detailedQuaternion(-roll,-noseUpPitch,heading);
  }
  root.DriveAttitudeAdapter={toFrame,toCompass,detailedQuaternion,fallbackQuaternion,rotate};
})(typeof window !== "undefined" ? window : globalThis);
