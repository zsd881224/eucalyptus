// volume model
//

define([
    './eucamodel'
], function(EucaModel) {
    var model = EucaModel.extend({
          namedColumns: ['id','snapshot_id','instance_id'],
          promote_ids: function(self) {
            var tmp = self.get('attach_data');
            if (tmp) self.set('instance_id', tmp.instance_id?tmp.instance_id:"");
          },
          
          validation: {

            // ====================
            // API Reference: 
            // http://docs.aws.amazon.com/AWSEC2/latest/APIReference/ApiReference-query-CreateVolume.html
            // http://docs.aws.amazon.com/AWSEC2/latest/APIReference/ApiReference-query-DeleteVolume.html
            // http://docs.aws.amazon.com/AWSEC2/latest/APIReference/ApiReference-query-AttachVolume.html
            // http://docs.aws.amazon.com/AWSEC2/latest/APIReference/ApiReference-query-DetachVolume.html
            // ====================

            volume_id: {
              required: false
            },
            name: {
              rangeLength: [1, 128],
              required: false
            },
            status: {
              oneOf: ['creating', 'created', 'attaching','attached', 'detaching', 'detached'],
              required: false
            },
            size: {
              pattern: 'digits',
              min: 1,
              max: 1024,
              required: true,
              msg: window['volume_create_invalid_size']
            },
            instance_id: {
              required: false
            },
            device: {
              required: false
            },
            attach_time: {
              pattern: /^\d{4}-\d{2}-\d{2}T\d{2}\:\d{2}\:\d{2}\.\w+/,
              required: false
            },
            force: {
              oneOf: ['true', 'false'],
              required: false
            },
            availablity_zone: {
              required: false
            },
            create_time: {
              pattern: /^\d{4}-\d{2}-\d{2}T\d{2}\:\d{2}\:\d{2}\.\w+/,
              required: false
            },
            volume_type: {
              oneOf: ['standard', 'io1'],
              required: false
            },
            iops: {
              min: 100,
              max: 2000,
              required: false
            },
            snapshot_id: {
              required: false
            },
          },
          sync: function(method, model, options){
            if(method == 'create'){
              return this.syncMethod_Create(model, options);
            }else if(method == 'delete'){
              return this.syncMethod_Delete(model, options);
            }
          },
          syncMethod_Create: function(model, options){
            var url = "/ec2?Action=CreateVolume";
            var size = model.get('size');
            var availability_zone = model.get('availablity_zone');
            var snapshot_id = model.get('snapshot_id');
            var parameter = "_xsrf="+$.cookie('_xsrf');
            parameter += "&Size="+size+"&AvailabilityZone="+availability_zone;
            if(snapshot_id != undefined){
              parameter += "&SnapshotId="+snapshot_id;
            }
            return this.makeAjaxCall(url, parameter, options);
          },
          syncMethod_Delete: function(model, options){
            var url = "/ec2?Action=DeleteVolume";
            var id = model.get('id');
            var parameter = "_xsrf="+$.cookie('_xsrf');
            parameter += "&VolumeId="+id;
            return this.makeAjaxCall(url, parameter, options);
          },

          attach: function(instance_id, device, options){
            var url = "/ec2?Action=AttachVolume";
            var volume_id = this.get('id');            // Need consistency in ID label  -- Kyo 040813
            var parameter = "_xsrf="+$.cookie('_xsrf');
            parameter += "&VolumeId="+volume_id+"&InstanceId="+instance_id+"&Device="+device;
            this.makeAjaxCall(url, parameter, options);
          },
          detach: function(options){
            var url = "/ec2?Action=DetachVolume";
            var volume_id = this.get('id');             // Need consistency in ID label  -- Kyo 040813
            var parameter = "_xsrf="+$.cookie('_xsrf');
            parameter += "&VolumeId="+volume_id;
            this.makeAjaxCall(url, parameter, options);
          },

    });
    return model;
});
